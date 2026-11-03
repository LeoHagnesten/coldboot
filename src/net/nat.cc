#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include "net/nat.hh"

namespace net {

	static const uint8_t gateway_mac [ 6 ] = { 0x02, 0xCB, 0x00, 0x00, 0x00, 0x02 };
	static constexpr uint32_t out_capacity = 0x40000;
	static constexpr uint32_t in_capacity = 0x10000;
	static constexpr uint8_t TCP_FIN = 0x01;
	static constexpr uint8_t TCP_SYN = 0x02;
	static constexpr uint8_t TCP_RST = 0x04;
	static constexpr uint8_t TCP_PSH = 0x08;
	static constexpr uint8_t TCP_ACK = 0x10;

	enum link_state : uint8_t {

		LINK_FREE,
		LINK_CONNECTING,
		LINK_ANSWERED,
		LINK_OPEN,

	};

	struct tcp_link {

		SOCKET socket = INVALID_SOCKET;
		uint32_t remote = 0;
		uint16_t remote_port = 0;
		uint16_t guest_port = 0;
		link_state state = LINK_FREE;
		uint32_t guest_next = 0;
		uint32_t start = 0;
		uint32_t acked = 0;
		uint32_t next = 0;
		uint32_t window = 0;
		uint32_t mss = 536;
		uint8_t* outgoing = nullptr;
		uint32_t out_size = 0;
		uint8_t* incoming = nullptr;
		uint32_t in_size = 0;
		uint32_t offered = 0;
		bool readable = false;
		bool writable = false;
		bool host_eof = false;
		bool guest_fin = false;
		bool fin_sent = false;
		bool fin_acked = false;
		bool shut = false;
		bool local = false;
		uint64_t sent_at = 0;
		uint32_t retries = 0;
		uint8_t proxy_stage = 0;
		uint32_t reply_size = 0;
		uint8_t reply [ 272 ] = { };
		char host [ 256 ] = { };

	};

	struct udp_flow {

		SOCKET socket = INVALID_SOCKET;
		uint32_t remote = 0;
		uint16_t remote_port = 0;
		uint16_t guest_port = 0;
		uint64_t used_at = 0;

	};

	struct echo_request {

		HANDLE done = nullptr;
		bool busy = false;
		uint32_t target = 0;
		uint16_t identifier = 0;
		uint16_t number = 0;
		uint8_t data [ 512 ] = { };
		uint32_t size = 0;
		uint8_t reply [ sizeof ( ICMP_ECHO_REPLY ) + 512 + 64 ] = { };

	};

	static uint16_t be16 ( const uint8_t* at ) {

		return static_cast < uint16_t > ( at [ 0 ] << 8 | at [ 1 ] );

	}

	static uint32_t be32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) << 24 | static_cast < uint32_t > ( at [ 1 ] ) << 16 | static_cast < uint32_t > ( at [ 2 ] ) << 8 | at [ 3 ];

	}

	static void put16 ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value >> 8 );
		at [ 1 ] = static_cast < uint8_t > ( value );

	}

	static void put32 ( uint8_t* at, uint32_t value ) {

		put16 ( at, value >> 16 );
		put16 ( at + 2, value );

	}

	static void move_bytes ( uint8_t* out, const uint8_t* in, uint32_t size ) {

		for ( uint32_t i = 0; i < size; i++ )
			out [ i ] = in [ i ];

	}

	static bool before ( uint32_t a, uint32_t b ) {

		return static_cast < int32_t > ( a - b ) < 0;

	}

	nat::nat ( ) {

		links = new tcp_link [ max_links ];
		flows = new udp_flow [ max_flows ];
		echoes = new echo_request [ max_echoes ];
		queue = static_cast < uint8_t* > ( runtime_allocate ( queue_slots * slot_size ) );
		scratch = static_cast < uint8_t* > ( runtime_allocate ( 0x10000 ) );
		names = new name_entry [ name_slots ] ( );
		gates = new gate_entry [ max_gates ] ( );
		requests = new access_request [ max_requests ] ( );

	}

	nat::~nat ( ) {

		stop ( );
		delete [ ] links;
		delete [ ] flows;
		delete [ ] echoes;
		delete [ ] names;
		delete [ ] gates;
		delete [ ] requests;
		runtime_release ( queue );
		runtime_release ( scratch );

	}

	bool nat::start ( port* guest ) {

		stop ( );
		target = guest;

		WSADATA data;

		if ( !sockets_ready ) {

			if ( WSAStartup ( MAKEWORD ( 2, 2 ), &data ) )
				return false;

			sockets_ready = true;

		}

		find_resolver ( );
		resolve_proxy ( );
		sequence_seed = static_cast < uint32_t > ( GetTickCount64 ( ) * 2654435761ull );
		quit = 0;
		queue_head = 0;
		queue_count = 0;
		wake = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
		activity = WSACreateEvent ( );
		icmp = IcmpCreateFile ( );

		for ( uint32_t i = 0; i < max_echoes; i++ )
			echoes [ i ].done = CreateEventW ( nullptr, FALSE, FALSE, nullptr );

		thread = CreateThread ( nullptr, 0, &loop, this, 0, nullptr );

		return thread != nullptr;

	}

	void nat::stop ( ) {

		if ( thread ) {

			InterlockedExchange ( &quit, 1 );
			SetEvent ( wake );
			WaitForSingleObject ( thread, INFINITE );
			CloseHandle ( thread );
			thread = nullptr;

		}

		for ( uint32_t i = 0; i < max_links; i++ ) {

			if ( links [ i ].state != LINK_FREE )
				close_link ( links [ i ], false );

		}

		for ( uint32_t i = 0; i < max_flows; i++ ) {

			if ( flows [ i ].socket != INVALID_SOCKET ) {

				closesocket ( flows [ i ].socket );
				flows [ i ].socket = INVALID_SOCKET;

			}

		}

		if ( icmp && icmp != INVALID_HANDLE_VALUE ) {

			for ( uint32_t i = 0; i < max_echoes; i++ ) {

				if ( echoes [ i ].busy )
					WaitForSingleObject ( echoes [ i ].done, 2000 );

				echoes [ i ].busy = false;

			}

			IcmpCloseHandle ( icmp );

		}

		icmp = nullptr;

		for ( uint32_t i = 0; i < max_echoes; i++ ) {

			if ( echoes [ i ].done )
				CloseHandle ( echoes [ i ].done );

			echoes [ i ].done = nullptr;

		}

		if ( wake )
			CloseHandle ( wake );

		if ( activity )
			WSACloseEvent ( activity );

		wake = nullptr;
		activity = nullptr;

		if ( sockets_ready ) {

			WSACleanup ( );
			sockets_ready = false;

		}

	}

	void nat::find_resolver ( ) {

		resolver_length = 0;

		ULONG size = 0x8000;
		IP_ADAPTER_ADDRESSES* adapters = static_cast < IP_ADAPTER_ADDRESSES* > ( runtime_allocate ( size ) );
		ULONG result = GetAdaptersAddresses ( AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME, nullptr, adapters, &size );

		if ( result == ERROR_BUFFER_OVERFLOW ) {

			runtime_release ( adapters );
			adapters = static_cast < IP_ADAPTER_ADDRESSES* > ( runtime_allocate ( size ) );
			result = GetAdaptersAddresses ( AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME, nullptr, adapters, &size );

		}

		for ( int32_t pass = 0; pass < 2 && result == NO_ERROR && !resolver_length; pass++ ) {

			for ( IP_ADAPTER_ADDRESSES* adapter = adapters; adapter && !resolver_length; adapter = adapter->Next ) {

				if ( adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK )
					continue;

				for ( IP_ADAPTER_DNS_SERVER_ADDRESS* server = adapter->FirstDnsServerAddress; server && !resolver_length; server = server->Next ) {

					sockaddr* address = server->Address.lpSockaddr;
					int32_t length = server->Address.iSockaddrLength;

					if ( !address || length <= 0 || length > static_cast < int32_t > ( sizeof ( resolver ) ) )
						continue;

					if ( ( pass == 0 && address->sa_family != AF_INET ) || ( pass == 1 && address->sa_family != AF_INET6 ) )
						continue;

					if ( address->sa_family == AF_INET6 && reinterpret_cast < sockaddr_in6* > ( address )->sin6_addr.u.Byte [ 0 ] == 0xFE && reinterpret_cast < sockaddr_in6* > ( address )->sin6_addr.u.Byte [ 1 ] == 0xC0 )
						continue;

					move_bytes ( resolver, reinterpret_cast < const uint8_t* > ( address ), static_cast < uint32_t > ( length ) );
					resolver_length = length;

				}

			}

		}

		runtime_release ( adapters );

	}

	bool nat::resolve_proxy ( ) {

		proxy_length = 0;

		if ( !proxy [ 0 ] )
			return false;

		char host [ 128 ] = { };
		int32_t colon = -1;

		for ( int32_t i = 0; proxy [ i ] && i < 127; i++ ) {

			host [ i ] = proxy [ i ];

			if ( proxy [ i ] == ':' )
				colon = i;

		}

		if ( colon < 1 )
			return false;

		host [ colon ] = 0;

		addrinfo hints = { };
		addrinfo* found = nullptr;

		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		if ( getaddrinfo ( host, host + colon + 1, &hints, &found ) || !found )
			return false;

		if ( found->ai_addrlen <= sizeof ( proxy_address ) ) {

			move_bytes ( proxy_address, reinterpret_cast < const uint8_t* > ( found->ai_addr ), static_cast < uint32_t > ( found->ai_addrlen ) );
			proxy_length = static_cast < int32_t > ( found->ai_addrlen );

		}

		freeaddrinfo ( found );

		return proxy_length > 0;

	}

	bool nat::private_address ( uint32_t address ) {

		return ( address >> 24 ) == 10 || ( address >> 24 ) == 127 || ( address >> 24 ) == 0 || ( address & 0xFFF00000u ) == 0xAC100000u || ( address & 0xFFFF0000u ) == 0xC0A80000u || ( address & 0xFFFF0000u ) == 0xA9FE0000u || ( address & 0xFFC00000u ) == 0x64400000u || address >= 0xE0000000u;

	}

	uint32_t nat::name_address ( const char* name ) {

		for ( uint32_t i = 0; i < name_slots; i++ ) {

			if ( names [ i ].address && base::same_text ( names [ i ].name, name ) )
				return names [ i ].address;

		}

		name_entry& slot = names [ name_next % name_slots ];
		int32_t length = 0;

		for ( ; name [ length ] && length < 255; length++ )
			slot.name [ length ] = name [ length ];

		slot.name [ length ] = 0;
		slot.address = synthetic_base + 1 + name_next % 0x1FFFE;
		name_next++;

		return slot.address;

	}

	const char* nat::address_name ( uint32_t address ) const {

		for ( uint32_t i = 0; i < name_slots; i++ ) {

			if ( names [ i ].address == address )
				return names [ i ].name;

		}

		return nullptr;

	}

	void nat::answer_dns ( uint16_t guest_port, const uint8_t* data, uint32_t size ) {

		if ( size < 12 || size > 512 || ( data [ 2 ] & 0x80 ) )
			return;

		uint8_t out [ 600 ];
		char name [ 256 ] = { };
		uint32_t used = 0;
		uint32_t at = 12;
		uint32_t labels = 0;
		bool broken = be16 ( data + 4 ) != 1 || ( data [ 2 ] >> 3 & 15 ) != 0;

		while ( !broken && at < size && data [ at ] ) {

			uint32_t length = data [ at ];

			if ( length > 63 || at + 1 + length >= size || used + length + 1 > 253 ) {

				broken = true;

				break;

			}

			if ( used )
				name [ used++ ] = '.';

			for ( uint32_t i = 0; i < length; i++ ) {

				char c = static_cast < char > ( data [ at + 1 + i ] );

				name [ used++ ] = c >= 'A' && c <= 'Z' ? static_cast < char > ( c | 0x20 ) : c;

			}

			labels++;
			at += 1 + length;

		}

		uint32_t question_end = at + 5;

		if ( broken || question_end > size ) {

			move_bytes ( out, data, 12 );
			out [ 2 ] = static_cast < uint8_t > ( 0x80 | ( data [ 2 ] & 0x79 ) );
			out [ 3 ] = 0x84;

			for ( int32_t i = 4; i < 12; i++ )
				out [ i ] = 0;

			udp_out ( resolver_address, 53, guest_address, guest_port, out, 12 );

			return;

		}

		uint16_t type = be16 ( data + at + 1 );
		uint16_t kind = be16 ( data + at + 3 );
		auto ends = [ & ] ( const char* tail ) {

			int32_t length = base::text_length ( tail );

			if ( static_cast < int32_t > ( used ) < length )
				return false;

			for ( int32_t i = 0; i < length; i++ ) {

				if ( name [ used - length + i ] != tail [ i ] )
					return false;

			}

			return static_cast < int32_t > ( used ) == length || name [ used - length - 1 ] == '.';

		};

		bool hidden = labels < 2 || ends ( "local" ) || ends ( "lan" ) || ends ( "home" ) || ends ( "internal" ) || ends ( "arpa" ) || ends ( "localhost" );
		uint32_t length = question_end;

		move_bytes ( out, data, question_end );
		out [ 2 ] = static_cast < uint8_t > ( 0x80 | ( data [ 2 ] & 0x01 ) );
		out [ 3 ] = static_cast < uint8_t > ( 0x80 | ( hidden ? 3 : 0 ) );
		put16 ( out + 4, 1 );
		put16 ( out + 6, 0 );
		put16 ( out + 8, 0 );
		put16 ( out + 10, 0 );

		if ( !hidden && type == 1 && kind == 1 ) {

			static const uint8_t record [ 10 ] = { 0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C };

			move_bytes ( out + length, record, 10 );
			put16 ( out + length + 10, 4 );
			put32 ( out + length + 12, name_address ( name ) );
			put16 ( out + 6, 1 );
			length += 16;

		}

		udp_out ( resolver_address, 53, guest_address, guest_port, out, length );

	}

	void nat::proxy_step ( tcp_link& link ) {

		while ( link.reply_size < sizeof ( link.reply ) ) {

			int got = recv ( link.socket, reinterpret_cast < char* > ( link.reply + link.reply_size ), static_cast < int > ( sizeof ( link.reply ) - link.reply_size ), 0 );

			if ( got == SOCKET_ERROR ) {

				if ( WSAGetLastError ( ) == WSAEWOULDBLOCK )
					link.readable = false;
				else
					got = 0;

				if ( got != 0 )
					break;

			}

			if ( got <= 0 ) {

				tcp_reset ( link.remote, link.remote_port, guest_address, link.guest_port, 0, link.guest_next );
				close_link ( link, false );

				return;

			}

			link.reply_size += static_cast < uint32_t > ( got );

		}

		auto fail = [ & ] ( ) {

			tcp_reset ( link.remote, link.remote_port, guest_address, link.guest_port, 0, link.guest_next );
			close_link ( link, false );

		};

		if ( link.proxy_stage == 2 && link.reply_size >= 2 ) {

			if ( link.reply [ 0 ] != 5 || link.reply [ 1 ] != 0 ) {

				fail ( );

				return;

			}

			uint8_t request [ 300 ];
			uint32_t length = 0;
			uint32_t named = static_cast < uint32_t > ( base::text_length ( link.host ) );

			request [ length++ ] = 5;
			request [ length++ ] = 1;
			request [ length++ ] = 0;

			if ( named ) {

				request [ length++ ] = 3;
				request [ length++ ] = static_cast < uint8_t > ( named );
				move_bytes ( request + length, reinterpret_cast < const uint8_t* > ( link.host ), named );
				length += named;

			} else {

				request [ length++ ] = 1;
				put32 ( request + length, link.remote );
				length += 4;

			}

			put16 ( request + length, link.remote_port );
			length += 2;
			move_bytes ( link.reply, link.reply + 2, link.reply_size - 2 );
			link.reply_size -= 2;
			link.proxy_stage = 3;

			if ( ::send ( link.socket, reinterpret_cast < const char* > ( request ), static_cast < int > ( length ), 0 ) != static_cast < int > ( length ) ) {

				fail ( );

				return;

			}

		}

		if ( link.proxy_stage == 3 && link.reply_size >= 5 ) {

			if ( link.reply [ 0 ] != 5 || link.reply [ 1 ] != 0 ) {

				fail ( );

				return;

			}

			uint32_t address = link.reply [ 3 ] == 1 ? 4u : ( link.reply [ 3 ] == 4 ? 16u : 1u + link.reply [ 4 ] );
			uint32_t total = 4 + address + 2;

			if ( link.reply_size < total )
				return;

			uint32_t extra = link.reply_size - total;

			move_bytes ( link.outgoing, link.reply + total, extra );
			link.out_size = extra;
			link.reply_size = 0;
			link.proxy_stage = 0;
			link.state = LINK_ANSWERED;
			link.sent_at = GetTickCount64 ( );
			link.readable = true;
			InterlockedIncrement ( &proxied );
			tcp_out ( link, TCP_SYN | TCP_ACK, link.start, nullptr, 0 );

		}

	}

	bool nat::gate ( uint8_t protocol, uint16_t guest_port, uint32_t remote, uint16_t remote_port, const uint8_t* segment, uint32_t size, const char* host ) {

		if ( !ask_first )
			return true;

		gate_entry* found = nullptr;
		gate_entry* spare = nullptr;
		bool pass = false;
		bool refuse = false;

		AcquireSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &gate_lock ) );

		for ( uint32_t i = 0; i < max_gates; i++ ) {

			gate_entry& item = gates [ i ];

			if ( !item.state ) {

				if ( !spare )
					spare = &item;

				continue;

			}

			if ( item.protocol == protocol && item.guest_port == guest_port && item.remote == remote && item.remote_port == remote_port )
				found = &item;

		}

		if ( !found && !spare ) {

			for ( uint32_t i = 0; i < max_gates; i++ ) {

				gate_entry& item = gates [ i ];
				bool better = !spare || ( item.state != 1 && spare->state == 1 ) || ( ( item.state != 1 ) == ( spare->state != 1 ) && item.since < spare->since );

				if ( better )
					spare = &item;

			}

		}

		if ( !found && spare && request_count >= max_requests ) {

			for ( uint32_t i = 1; i < request_count; i++ )
				requests [ i - 1 ] = requests [ i ];

			request_count--;

		}

		if ( found && found->state == 2 ) {

			pass = true;
			*found = gate_entry ( );

		} else if ( found && found->state == 3 )
			refuse = protocol == 6;
		else if ( !found && spare ) {

			*spare = gate_entry ( );
			spare->id = ++gate_serial;
			spare->protocol = protocol;
			spare->state = 1;
			spare->guest_port = guest_port;
			spare->remote = remote;
			spare->remote_port = remote_port;
			spare->since = GetTickCount64 ( );
			spare->segment_size = size < sizeof ( spare->segment ) ? size : static_cast < uint32_t > ( sizeof ( spare->segment ) );

			if ( segment )
				move_bytes ( spare->segment, segment, spare->segment_size );

			access_request& item = requests [ request_count++ ];
			const char* named = host ? host : address_name ( remote );

			item = access_request ( );
			item.id = spare->id;
			item.protocol = protocol;
			item.guest_port = guest_port;
			item.remote = remote;
			item.remote_port = remote_port;

			for ( int32_t i = 0; named && named [ i ] && i < 255; i++ )
				item.host [ i ] = named [ i ];

		}

		ReleaseSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &gate_lock ) );

		if ( refuse && segment && size >= 8 )
			tcp_reset ( remote, remote_port, guest_address, guest_port, 0, be32 ( segment + 4 ) + 1 );

		return pass;

	}

	bool nat::next_request ( access_request& out ) {

		AcquireSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &gate_lock ) );

		bool found = request_count > 0;

		if ( found ) {

			out = requests [ 0 ];

			for ( uint32_t i = 1; i < request_count; i++ )
				requests [ i - 1 ] = requests [ i ];

			request_count--;

		}

		ReleaseSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &gate_lock ) );

		return found;

	}

	void nat::decide ( uint32_t id, bool allow ) {

		AcquireSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &gate_lock ) );

		for ( uint32_t i = 0; i < max_gates; i++ ) {

			if ( gates [ i ].state == 1 && gates [ i ].id == id ) {

				gates [ i ].state = allow ? 2 : 3;
				gates [ i ].since = GetTickCount64 ( );

			}

		}

		ReleaseSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &gate_lock ) );
		kick ( );

	}

	void nat::service_gates ( uint64_t now ) {

		if ( !ask_first )
			return;

		gate_entry replay [ 8 ];
		gate_entry refused [ 8 ];
		uint32_t replays = 0;
		uint32_t refusals = 0;

		AcquireSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &gate_lock ) );

		for ( uint32_t i = 0; i < max_gates; i++ ) {

			gate_entry& item = gates [ i ];

			if ( !item.state )
				continue;

			if ( item.state == 2 && item.protocol == 6 && !item.handled && replays < 8 ) {

				item.handled = true;
				replay [ replays++ ] = item;

			} else if ( item.state == 3 && item.protocol == 6 && !item.handled && refusals < 8 ) {

				item.handled = true;
				refused [ refusals++ ] = item;

			}

			if ( ( item.state == 1 && now - item.since > 120000 ) || ( item.state != 1 && now - item.since > 60000 ) )
				item = gate_entry ( );

		}

		ReleaseSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &gate_lock ) );

		for ( uint32_t i = 0; i < replays; i++ ) {

			InterlockedIncrement ( &replayed );
			handle_tcp ( guest_address, replay [ i ].remote, replay [ i ].segment, replay [ i ].segment_size );

		}

		for ( uint32_t i = 0; i < refusals; i++ ) {

			if ( refused [ i ].segment_size >= 8 )
				tcp_reset ( refused [ i ].remote, refused [ i ].remote_port, guest_address, refused [ i ].guest_port, 0, be32 ( refused [ i ].segment + 4 ) + 1 );

		}

	}

	void nat::remember_names ( const uint8_t* data, uint32_t size ) {

		if ( size < 12 || !( data [ 2 ] & 0x80 ) || be16 ( data + 4 ) != 1 )
			return;

		char name [ 256 ] = { };
		uint32_t used = 0;
		uint32_t at = 12;

		while ( at < size && data [ at ] ) {

			uint32_t length = data [ at ];

			if ( length > 63 || at + 1 + length > size || used + length + 1 > 253 )
				return;

			if ( used )
				name [ used++ ] = '.';

			for ( uint32_t i = 0; i < length; i++ ) {

				char c = static_cast < char > ( data [ at + 1 + i ] );

				name [ used++ ] = c >= 'A' && c <= 'Z' ? static_cast < char > ( c | 0x20 ) : c;

			}

			at += 1 + length;

		}

		at += 5;

		uint32_t answers = be16 ( data + 6 );

		for ( uint32_t i = 0; i < answers && at < size; i++ ) {

			if ( ( data [ at ] & 0xC0 ) == 0xC0 )
				at += 2;
			else {

				while ( at < size && data [ at ] )
					at += 1u + data [ at ];

				at++;

			}

			if ( at + 10 > size )
				return;

			uint16_t type = be16 ( data + at );
			uint32_t length = be16 ( data + at + 8 );

			at += 10;

			if ( at + length > size )
				return;

			if ( type == 1 && length == 4 && used ) {

				name_entry& slot = names [ name_next % name_slots ];

				for ( uint32_t c = 0; c <= used; c++ )
					slot.name [ c ] = name [ c ];

				slot.address = be32 ( data + at );
				name_next++;

			}

			at += length;

		}

	}

	void nat::kick ( ) {

		if ( wake )
			SetEvent ( wake );

	}

	void nat::send ( const uint8_t* frame, uint32_t size ) {

		if ( !thread || size > slot_size || size < 14 )
			return;

		AcquireSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &queue_lock ) );

		if ( queue_count < queue_slots ) {

			uint32_t slot = ( queue_head + queue_count ) % queue_slots;

			move_bytes ( queue + slot * slot_size, frame, size );
			queue_sizes [ slot ] = size;
			queue_count++;

		} else
			InterlockedIncrement ( &dropped );

		ReleaseSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &queue_lock ) );
		SetEvent ( wake );

	}

	unsigned long __stdcall nat::loop ( void* context ) {

		static_cast < nat* > ( context )->run ( );

		return 0;

	}

	void nat::run ( ) {

		uint8_t* frame = static_cast < uint8_t* > ( runtime_allocate ( slot_size ) );
		HANDLE waits [ 2 ] = { wake, activity };

		while ( !quit ) {

			bool echoing = false;

			for ( uint32_t i = 0; i < max_echoes; i++ )
				echoing = echoing || echoes [ i ].busy;

			WaitForMultipleObjects ( 2, waits, FALSE, echoing ? 5 : 40 );

			if ( quit )
				break;

			for ( ;; ) {

				uint32_t size = 0;

				AcquireSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &queue_lock ) );

				if ( queue_count ) {

					size = queue_sizes [ queue_head ];
					move_bytes ( frame, queue + queue_head * slot_size, size );
					queue_head = ( queue_head + 1 ) % queue_slots;
					queue_count--;

				}

				ReleaseSRWLockExclusive ( reinterpret_cast < PSRWLOCK > ( &queue_lock ) );

				if ( !size )
					break;

				process ( frame, size );

			}

			service_all ( );

		}

		runtime_release ( frame );

	}

	void nat::service_all ( ) {

		uint64_t now = GetTickCount64 ( );

		if ( activity )
			WSAResetEvent ( activity );

		service_gates ( now );
		service_links ( now );
		service_flows ( now );
		service_echoes ( );

	}

	void nat::process ( const uint8_t* frame, uint32_t size ) {

		InterlockedIncrement ( &frames_in );

		uint16_t type = be16 ( frame + 12 );

		if ( !( frame [ 6 ] & 1 ) ) {

			move_bytes ( guest_mac, frame + 6, 6 );
			guest_known = true;

		}

		if ( type == 0x0806 )
			handle_arp ( frame, size );
		else if ( type == 0x0800 )
			handle_ip ( frame + 14, size - 14 );

	}

	void nat::handle_arp ( const uint8_t* frame, uint32_t size ) {

		if ( size < 42 )
			return;

		const uint8_t* arp = frame + 14;

		if ( be16 ( arp ) != 1 || be16 ( arp + 2 ) != 0x0800 || be16 ( arp + 6 ) != 1 )
			return;

		uint32_t sender = be32 ( arp + 14 );
		uint32_t wanted = be32 ( arp + 24 );

		if ( ( wanted & 0xFFFFFF00u ) != ( gateway_address & 0xFFFFFF00u ) || wanted == guest_address || wanted == sender )
			return;

		uint8_t reply [ 60 ] = { };

		move_bytes ( reply, arp + 8, 6 );
		move_bytes ( reply + 6, gateway_mac, 6 );
		put16 ( reply + 12, 0x0806 );
		put16 ( reply + 14, 1 );
		put16 ( reply + 16, 0x0800 );
		reply [ 18 ] = 6;
		reply [ 19 ] = 4;
		put16 ( reply + 20, 2 );
		move_bytes ( reply + 22, gateway_mac, 6 );
		put32 ( reply + 28, wanted );
		move_bytes ( reply + 32, arp + 8, 6 );
		put32 ( reply + 38, sender );
		InterlockedIncrement ( &frames_out );
		target->receive ( reply, 60 );

	}

	void nat::handle_ip ( const uint8_t* packet, uint32_t size ) {

		if ( size < 20 || packet [ 0 ] >> 4 != 4 )
			return;

		uint32_t header = ( packet [ 0 ] & 0x0F ) * 4u;
		uint32_t total = be16 ( packet + 2 );

		if ( header < 20 || total < header || total > size )
			return;

		if ( be16 ( packet + 6 ) & 0x3FFF )
			return;

		uint32_t source = be32 ( packet + 12 );
		uint32_t destination = be32 ( packet + 16 );
		const uint8_t* body = packet + header;
		uint32_t length = total - header;

		switch ( packet [ 9 ] ) {

			case 1:
				handle_icmp ( source, destination, body, length );
				break;

			case 6:
				handle_tcp ( source, destination, body, length );
				break;

			case 17:
				handle_udp ( source, destination, body, length );
				break;

			default:
				break;

		}

	}

	void nat::ip_out ( uint8_t protocol, uint32_t source, uint32_t destination, const uint8_t* head, uint32_t head_size, const uint8_t* body, uint32_t body_size ) {

		uint8_t frame [ 1600 ];
		uint32_t total = 20 + head_size + body_size;

		if ( 14 + total > sizeof ( frame ) || !target )
			return;

		bool broadcast = destination == 0xFFFFFFFFu || !guest_known;

		for ( int32_t i = 0; i < 6; i++ )
			frame [ i ] = broadcast ? 0xFF : guest_mac [ i ];

		move_bytes ( frame + 6, gateway_mac, 6 );
		put16 ( frame + 12, 0x0800 );

		uint8_t* ip = frame + 14;

		ip [ 0 ] = 0x45;
		ip [ 1 ] = 0;
		put16 ( ip + 2, total );
		put16 ( ip + 4, identity++ );
		put16 ( ip + 6, 0x4000 );
		ip [ 8 ] = 64;
		ip [ 9 ] = protocol;
		put16 ( ip + 10, 0 );
		put32 ( ip + 12, source );
		put32 ( ip + 16, destination );
		put16 ( ip + 10, fold ( sum_words ( ip, 20, 0 ) ) );

		uint8_t* layer = ip + 20;

		move_bytes ( layer, head, head_size );
		move_bytes ( layer + head_size, body, body_size );

		uint32_t length = head_size + body_size;

		if ( protocol == 1 ) {

			put16 ( layer + 2, 0 );
			put16 ( layer + 2, fold ( sum_words ( layer, length, 0 ) ) );

		} else {

			uint32_t pseudo = sum_words ( ip + 12, 8, 0 ) + protocol + length;
			uint32_t at = protocol == 6 ? 16 : 6;

			put16 ( layer + at, 0 );

			uint16_t sum = fold ( sum_words ( layer, length, pseudo ) );

			put16 ( layer + at, protocol == 17 && !sum ? 0xFFFF : sum );

		}

		InterlockedIncrement ( &frames_out );
		target->receive ( frame, 14 + total );

	}

	void nat::handle_icmp ( uint32_t source, uint32_t destination, const uint8_t* data, uint32_t size ) {

		if ( size < 8 || data [ 0 ] != 8 )
			return;

		if ( ( destination & 0xFFFFFF00u ) == ( gateway_address & 0xFFFFFF00u ) ) {

			if ( destination != gateway_address && destination != resolver_address )
				return;

			uint8_t head [ 8 ];

			move_bytes ( head, data, 8 );
			head [ 0 ] = 0;
			ip_out ( 1, destination, source, head, 8, data + 8, size - 8 );

			return;

		}

		if ( isolated || !icmp || icmp == INVALID_HANDLE_VALUE || destination >= 0xE0000000u || ( proxy [ 0 ] && !private_address ( destination ) ) || ( !local_network && private_address ( destination ) ) ) {

			InterlockedIncrement ( &blocked );

			return;

		}

		for ( uint32_t i = 0; i < max_echoes; i++ ) {

			echo_request& item = echoes [ i ];

			if ( item.busy )
				continue;

			item.target = destination;
			item.identifier = be16 ( data + 4 );
			item.number = be16 ( data + 6 );
			item.size = size - 8 < sizeof ( item.data ) ? size - 8 : static_cast < uint32_t > ( sizeof ( item.data ) );
			move_bytes ( item.data, data + 8, item.size );

			IPAddr where = htonl ( destination );
			DWORD result = IcmpSendEcho2 ( icmp, item.done, nullptr, nullptr, where, item.data, static_cast < WORD > ( item.size ), nullptr, item.reply, sizeof ( item.reply ), 2000 );

			item.busy = result != 0 || GetLastError ( ) == ERROR_IO_PENDING;

			return;

		}

	}

	void nat::service_echoes ( ) {

		for ( uint32_t i = 0; i < max_echoes; i++ ) {

			echo_request& item = echoes [ i ];

			if ( !item.busy || WaitForSingleObject ( item.done, 0 ) != WAIT_OBJECT_0 )
				continue;

			item.busy = false;

			if ( !IcmpParseReplies ( item.reply, sizeof ( item.reply ) ) )
				continue;

			const ICMP_ECHO_REPLY* reply = reinterpret_cast < const ICMP_ECHO_REPLY* > ( item.reply );

			if ( reply->Status != IP_SUCCESS )
				continue;

			uint8_t head [ 8 ] = { 0, 0, 0, 0 };

			put16 ( head + 4, item.identifier );
			put16 ( head + 6, item.number );
			ip_out ( 1, item.target, guest_address, head, 8, item.data, item.size );

		}

	}

	void nat::dhcp ( const uint8_t* data, uint32_t size ) {

		if ( size < 240 || data [ 0 ] != 1 || be32 ( data + 236 ) != 0x63825363 )
			return;

		uint8_t kind = 0;

		for ( uint32_t at = 240; at < size; ) {

			uint8_t code = data [ at ];

			if ( code == 0 ) {

				at++;

				continue;

			}

			if ( code == 255 || at + 1 >= size )
				break;

			uint8_t length = data [ at + 1 ];

			if ( code == 53 && length >= 1 && at + 2 < size )
				kind = data [ at + 2 ];

			at += 2u + length;

		}

		uint8_t reply_kind = kind == 1 ? 2 : ( kind == 3 || kind == 8 ? 5 : 0 );

		if ( !reply_kind )
			return;

		uint8_t reply [ 300 ] = { };

		reply [ 0 ] = 2;
		reply [ 1 ] = 1;
		reply [ 2 ] = 6;
		move_bytes ( reply + 4, data + 4, 4 );
		move_bytes ( reply + 10, data + 10, 2 );

		if ( kind == 8 )
			move_bytes ( reply + 12, data + 12, 4 );
		else
			put32 ( reply + 16, guest_address );

		put32 ( reply + 20, gateway_address );
		move_bytes ( reply + 28, data + 28, 16 );
		put32 ( reply + 236, 0x63825363 );

		uint32_t at = 240;
		auto option = [ & ] ( uint8_t code, uint32_t value, uint8_t length ) {

			reply [ at++ ] = code;
			reply [ at++ ] = length;

			for ( int32_t i = length - 1; i >= 0; i-- )
				reply [ at++ ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

		};

		option ( 53, reply_kind, 1 );
		option ( 54, gateway_address, 4 );

		if ( kind != 8 )
			option ( 51, 86400, 4 );

		option ( 1, 0xFFFFFF00u, 4 );
		option ( 3, gateway_address, 4 );
		option ( 6, resolver_address, 4 );
		reply [ at++ ] = 255;
		udp_out ( gateway_address, 67, 0xFFFFFFFFu, 68, reply, at < 300 ? 300 : at );

	}

	void nat::udp_out ( uint32_t source, uint16_t source_port, uint32_t destination, uint16_t destination_port, const uint8_t* data, uint32_t size ) {

		uint8_t head [ 8 ];

		put16 ( head, source_port );
		put16 ( head + 2, destination_port );
		put16 ( head + 4, 8 + size );
		put16 ( head + 6, 0 );
		ip_out ( 17, source, destination, head, 8, data, size );

	}

	bool nat::map_target ( uint32_t address, uint16_t number, void* out, int32_t& length ) {

		if ( isolated )
			return false;

		if ( address == resolver_address && number == 53 ) {

			if ( !resolver_length || proxy [ 0 ] )
				return false;

			move_bytes ( static_cast < uint8_t* > ( out ), resolver, static_cast < uint32_t > ( resolver_length ) );

			if ( reinterpret_cast < sockaddr* > ( out )->sa_family == AF_INET )
				reinterpret_cast < sockaddr_in* > ( out )->sin_port = htons ( 53 );
			else
				reinterpret_cast < sockaddr_in6* > ( out )->sin6_port = htons ( 53 );

			length = resolver_length;

			return true;

		}

		if ( ( address & 0xFFFFFF00u ) == ( gateway_address & 0xFFFFFF00u ) && address != gateway_address )
			return false;

		if ( address >= 0xE0000000u || !address )
			return false;

		if ( !local_network && ( private_address ( address ) || address == gateway_address ) ) {

			InterlockedIncrement ( &blocked );

			return false;

		}

		if ( proxy [ 0 ] && !private_address ( address ) ) {

			InterlockedIncrement ( &blocked );

			return false;

		}

		sockaddr_in* place = static_cast < sockaddr_in* > ( out );

		*place = { };
		place->sin_family = AF_INET;
		place->sin_port = htons ( number );
		place->sin_addr.s_addr = htonl ( address == gateway_address ? 0x7F000001u : address );
		length = sizeof ( sockaddr_in );

		return true;

	}

	void nat::handle_udp ( uint32_t source, uint32_t destination, const uint8_t* data, uint32_t size ) {

		if ( size < 8 )
			return;

		uint16_t source_port = be16 ( data );
		uint16_t destination_port = be16 ( data + 2 );
		uint32_t length = be16 ( data + 4 );

		if ( length < 8 || length > size )
			return;

		const uint8_t* payload = data + 8;
		uint32_t payload_size = length - 8;

		if ( destination_port == 67 ) {

			dhcp ( payload, payload_size );

			return;

		}

		if ( proxy [ 0 ] && !isolated && destination == resolver_address && destination_port == 53 ) {

			answer_dns ( source_port, payload, payload_size );

			return;

		}

		uint8_t address [ 28 ];
		int32_t address_length = 0;

		if ( !map_target ( destination, destination_port, address, address_length ) )
			return;

		udp_flow* flow = nullptr;
		udp_flow* spare = nullptr;
		udp_flow* oldest = nullptr;

		for ( uint32_t i = 0; i < max_flows; i++ ) {

			udp_flow& item = flows [ i ];

			if ( item.socket == INVALID_SOCKET ) {

				if ( !spare )
					spare = &item;

				continue;

			}

			if ( item.guest_port == source_port && item.remote == destination && item.remote_port == destination_port )
				flow = &item;

			if ( !oldest || item.used_at < oldest->used_at )
				oldest = &item;

		}

		if ( !flow ) {

			if ( destination != resolver_address && !gate ( 17, source_port, destination, destination_port, nullptr, 0, nullptr ) )
				return;

			if ( !spare ) {

				closesocket ( oldest->socket );
				oldest->socket = INVALID_SOCKET;
				spare = oldest;

			}

			SOCKET handle = socket ( reinterpret_cast < sockaddr* > ( address )->sa_family, SOCK_DGRAM, IPPROTO_UDP );

			if ( handle == INVALID_SOCKET )
				return;

			if ( connect ( handle, reinterpret_cast < sockaddr* > ( address ), address_length ) == SOCKET_ERROR ) {

				closesocket ( handle );

				return;

			}

			WSAEventSelect ( handle, activity, FD_READ );
			flow = spare;
			flow->socket = handle;
			flow->guest_port = source_port;
			flow->remote = destination;
			flow->remote_port = destination_port;

		}

		flow->used_at = GetTickCount64 ( );
		::send ( flow->socket, reinterpret_cast < const char* > ( payload ), static_cast < int > ( payload_size ), 0 );

	}

	void nat::service_flows ( uint64_t now ) {

		for ( uint32_t i = 0; i < max_flows; i++ ) {

			udp_flow& flow = flows [ i ];

			if ( flow.socket == INVALID_SOCKET )
				continue;

			for ( int32_t burst = 0; burst < 64; burst++ ) {

				int got = recv ( flow.socket, reinterpret_cast < char* > ( scratch ), 1472, 0 );

				if ( got == SOCKET_ERROR ) {

					int error = WSAGetLastError ( );

					if ( error == WSAEWOULDBLOCK )
						break;

					if ( error == WSAEMSGSIZE || error == WSAECONNRESET )
						continue;

					break;

				}

				flow.used_at = now;

				if ( ask_first && flow.remote == resolver_address && flow.remote_port == 53 )
					remember_names ( scratch, static_cast < uint32_t > ( got ) );

				udp_out ( flow.remote, flow.remote_port, guest_address, flow.guest_port, scratch, static_cast < uint32_t > ( got ) );

			}

			if ( now - flow.used_at > ( flow.remote_port == 53 ? 15000u : 120000u ) ) {

				closesocket ( flow.socket );
				flow.socket = INVALID_SOCKET;

			}

		}

	}

	uint16_t nat::advertised ( const tcp_link& link ) const {

		uint32_t room = in_capacity - link.in_size;

		return static_cast < uint16_t > ( room > 0xFFFF ? 0xFFFF : room );

	}

	void nat::tcp_out ( tcp_link& link, uint8_t flags, uint32_t sequence, const uint8_t* data, uint32_t size ) {

		uint8_t head [ 24 ] = { };
		uint32_t length = flags & TCP_SYN ? 24 : 20;
		uint16_t window = advertised ( link );

		put16 ( head, link.remote_port );
		put16 ( head + 2, link.guest_port );
		put32 ( head + 4, sequence );
		put32 ( head + 8, flags & TCP_ACK ? link.guest_next : 0 );
		head [ 12 ] = static_cast < uint8_t > ( length / 4 << 4 );
		head [ 13 ] = flags;
		put16 ( head + 14, window );

		if ( flags & TCP_SYN ) {

			head [ 20 ] = 2;
			head [ 21 ] = 4;
			put16 ( head + 22, 1460 );

		}

		link.offered = window;
		ip_out ( 6, link.remote, guest_address, head, length, data, size );

	}

	uint32_t nat::links_in_use ( ) const {

		uint32_t used = 0;

		for ( uint32_t i = 0; i < max_links; i++ )
			used += links [ i ].state != LINK_FREE ? 1u : 0u;

		return used;

	}

	void nat::tcp_reset ( uint32_t source, uint16_t source_port, uint32_t destination, uint16_t destination_port, uint32_t sequence, uint32_t acknowledge ) {

		uint8_t head [ 20 ] = { };

		InterlockedIncrement ( &resets );

		put16 ( head, source_port );
		put16 ( head + 2, destination_port );
		put32 ( head + 4, sequence );
		put32 ( head + 8, acknowledge );
		head [ 12 ] = 0x50;
		head [ 13 ] = TCP_RST | TCP_ACK;
		ip_out ( 6, source, destination, head, 20, nullptr, 0 );

	}

	void nat::close_link ( tcp_link& link, bool reset ) {

		if ( reset && link.state != LINK_FREE )
			tcp_reset ( link.remote, link.remote_port, guest_address, link.guest_port, link.next, link.guest_next );

		if ( link.local && local )
			local->closed ( );

		if ( link.socket != INVALID_SOCKET ) {

			if ( reset ) {

				linger stop_now = { 1, 0 };

				setsockopt ( link.socket, SOL_SOCKET, SO_LINGER, reinterpret_cast < const char* > ( &stop_now ), sizeof ( stop_now ) );

			}

			closesocket ( link.socket );

		}

		runtime_release ( link.outgoing );
		runtime_release ( link.incoming );
		link = tcp_link ( );

	}

	void nat::handle_tcp ( uint32_t source, uint32_t destination, const uint8_t* data, uint32_t size ) {

		if ( size < 20 )
			return;

		uint16_t source_port = be16 ( data );
		uint16_t destination_port = be16 ( data + 2 );
		uint32_t sequence = be32 ( data + 4 );
		uint32_t acknowledge = be32 ( data + 8 );
		uint32_t header = ( data [ 12 ] >> 4 ) * 4u;
		uint8_t flags = data [ 13 ];
		uint32_t window = be16 ( data + 14 );

		if ( header < 20 || header > size )
			return;

		const uint8_t* payload = data + header;
		uint32_t payload_size = size - header;
		tcp_link* link = nullptr;

		for ( uint32_t i = 0; i < max_links && !link; i++ ) {

			tcp_link& item = links [ i ];

			if ( item.state != LINK_FREE && item.guest_port == source_port && item.remote == destination && item.remote_port == destination_port )
				link = &item;

		}

		if ( flags & TCP_RST ) {

			if ( link ) {

				InterlockedIncrement ( &guest_resets );
				close_link ( *link, false );

			}

			return;

		}

		if ( !link ) {

			if ( ( flags & ( TCP_SYN | TCP_ACK ) ) != TCP_SYN ) {

				if ( !( flags & TCP_RST ) )
					tcp_reset ( destination, destination_port, source, source_port, flags & TCP_ACK ? acknowledge : 0, sequence + payload_size + ( flags & TCP_SYN ? 1 : 0 ) + ( flags & TCP_FIN ? 1 : 0 ) );

				return;

			}

			uint8_t address [ 28 ];
			int32_t address_length = 0;

			if ( destination == tools_address ) {

				if ( destination_port != tools_port || !local ) {

					tcp_reset ( destination, destination_port, source, source_port, 0, sequence + 1 );

					return;

				}

				for ( uint32_t i = 0; i < max_links; i++ ) {

					if ( links [ i ].state != LINK_FREE && links [ i ].local )
						close_link ( links [ i ], true );

				}

			}

			for ( uint32_t i = 0; i < max_links && !link; i++ ) {

				if ( links [ i ].state == LINK_FREE )
					link = &links [ i ];

			}

			if ( link && destination == tools_address ) {

				*link = tcp_link ( );
				link->local = true;
				link->remote = destination;
				link->remote_port = destination_port;
				link->guest_port = source_port;
				link->state = LINK_ANSWERED;
				link->guest_next = sequence + 1;
				link->start = sequence_seed;
				sequence_seed += 0x01000193u;
				link->acked = link->start;
				link->next = link->start;
				link->window = window;
				link->mss = 1460;
				link->outgoing = static_cast < uint8_t* > ( runtime_allocate ( out_capacity ) );
				link->incoming = static_cast < uint8_t* > ( runtime_allocate ( in_capacity ) );
				link->sent_at = GetTickCount64 ( );
				tcp_out ( *link, TCP_SYN | TCP_ACK, link->start, nullptr, 0 );
				local->opened ( );

				return;

			}

			bool via_proxy = proxy [ 0 ] && !isolated && !private_address ( destination );
			bool synthetic = ( destination & 0xFFFE0000u ) == synthetic_base;
			const char* named = via_proxy && synthetic ? address_name ( destination ) : nullptr;
			bool reachable = via_proxy ? proxy_length > 0 && ( named || !synthetic ) : map_target ( destination, destination_port, address, address_length );

			if ( via_proxy && reachable ) {

				move_bytes ( address, proxy_address, static_cast < uint32_t > ( proxy_length ) );
				address_length = proxy_length;

			}

			if ( !link || !reachable ) {

				if ( via_proxy )
					InterlockedIncrement ( &blocked );

				tcp_reset ( destination, destination_port, source, source_port, 0, sequence + 1 );

				return;

			}

			if ( !gate ( 6, source_port, destination, destination_port, data, size, named ) )
				return;

			SOCKET handle = socket ( reinterpret_cast < sockaddr* > ( address )->sa_family, SOCK_STREAM, IPPROTO_TCP );

			if ( handle == INVALID_SOCKET ) {

				tcp_reset ( destination, destination_port, source, source_port, 0, sequence + 1 );

				return;

			}

			BOOL quick = TRUE;

			setsockopt ( handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast < const char* > ( &quick ), sizeof ( quick ) );
			WSAEventSelect ( handle, activity, FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE );

			if ( connect ( handle, reinterpret_cast < sockaddr* > ( address ), address_length ) == SOCKET_ERROR && WSAGetLastError ( ) != WSAEWOULDBLOCK ) {

				closesocket ( handle );
				tcp_reset ( destination, destination_port, source, source_port, 0, sequence + 1 );

				return;

			}

			InterlockedIncrement ( &connects );
			*link = tcp_link ( );
			link->socket = handle;
			link->remote = destination;
			link->remote_port = destination_port;
			link->guest_port = source_port;
			link->state = LINK_CONNECTING;
			link->guest_next = sequence + 1;
			link->start = sequence_seed;
			sequence_seed += 0x01000193u;
			link->acked = link->start;
			link->next = link->start;
			link->window = window;
			link->outgoing = static_cast < uint8_t* > ( runtime_allocate ( out_capacity ) );
			link->incoming = static_cast < uint8_t* > ( runtime_allocate ( in_capacity ) );
			link->sent_at = GetTickCount64 ( );
			link->proxy_stage = via_proxy ? 1 : 0;

			for ( int32_t i = 0; named && named [ i ] && i < 255; i++ ) {

				link->host [ i ] = named [ i ];
				link->host [ i + 1 ] = 0;

			}

			for ( uint32_t at = 20; at + 1 < header; ) {

				uint8_t kind = data [ at ];

				if ( kind == 0 )
					break;

				if ( kind == 1 ) {

					at++;

					continue;

				}

				uint8_t length = data [ at + 1 ];

				if ( length < 2 )
					break;

				if ( kind == 2 && length == 4 && at + 3 < header )
					link->mss = be16 ( data + at + 2 );

				at += length;

			}

			if ( link->mss > 1460 || link->mss < 64 )
				link->mss = link->mss < 64 ? 536 : 1460;

			return;

		}

		if ( flags & TCP_SYN ) {

			if ( link->state == LINK_ANSWERED )
				tcp_out ( *link, TCP_SYN | TCP_ACK, link->start, nullptr, 0 );

			return;

		}

		if ( !( flags & TCP_ACK ) || link->state == LINK_CONNECTING )
			return;

		if ( link->state == LINK_ANSWERED ) {

			if ( acknowledge != link->start + 1 )
				return;

			InterlockedIncrement ( &handshakes );
			link->state = LINK_OPEN;
			link->acked = acknowledge;
			link->next = acknowledge;
			link->retries = 0;

		}

		link->window = window;

		if ( !before ( link->next, acknowledge ) && before ( link->acked, acknowledge ) ) {

			uint32_t delta = acknowledge - link->acked;
			uint32_t data_acked = delta < link->out_size ? delta : link->out_size;

			move_bytes ( link->outgoing, link->outgoing + data_acked, link->out_size - data_acked );
			link->out_size -= data_acked;
			link->acked = acknowledge;
			link->retries = 0;
			link->sent_at = GetTickCount64 ( );

			if ( delta > data_acked && link->fin_sent )
				link->fin_acked = true;

		}

		bool answer = false;

		if ( payload_size || ( flags & TCP_FIN ) ) {

			if ( sequence == link->guest_next && !link->guest_fin ) {

				uint32_t room = in_capacity - link->in_size;
				uint32_t taken = payload_size < room ? payload_size : room;

				move_bytes ( link->incoming + link->in_size, payload, taken );
				link->in_size += taken;
				link->guest_next += taken;

				if ( ( flags & TCP_FIN ) && taken == payload_size ) {

					link->guest_next++;
					link->guest_fin = true;

				}

			}

			answer = true;

		}

		flush ( *link );
		fill ( *link );

		uint32_t before_next = link->next;

		pump ( *link );

		if ( answer && link->next == before_next && link->state != LINK_FREE )
			tcp_out ( *link, TCP_ACK, link->next, nullptr, 0 );

		if ( link->state != LINK_FREE && link->guest_fin && link->fin_acked && !link->in_size )
			close_link ( *link, false );

	}

	void nat::flush ( tcp_link& link ) {

		if ( link.local ) {

			if ( link.in_size && local )
				local->received ( link.incoming, link.in_size );

			link.in_size = 0;

			if ( link.guest_fin )
				link.host_eof = true;

			if ( link.state == LINK_OPEN && link.offered < link.mss && advertised ( link ) >= link.mss * 2 )
				tcp_out ( link, TCP_ACK, link.next, nullptr, 0 );

			return;

		}

		while ( link.state != LINK_FREE && link.writable && link.in_size ) {

			int sent = ::send ( link.socket, reinterpret_cast < const char* > ( link.incoming ), static_cast < int > ( link.in_size ), 0 );

			if ( sent == SOCKET_ERROR ) {

				if ( WSAGetLastError ( ) == WSAEWOULDBLOCK )
					link.writable = false;
				else
					close_link ( link, true );

				return;

			}

			move_bytes ( link.incoming, link.incoming + sent, link.in_size - static_cast < uint32_t > ( sent ) );
			link.in_size -= static_cast < uint32_t > ( sent );

		}

		if ( link.state != LINK_FREE && link.guest_fin && !link.in_size && !link.shut ) {

			shutdown ( link.socket, SD_SEND );
			link.shut = true;

		}

		if ( link.state == LINK_OPEN && link.offered < link.mss && advertised ( link ) >= link.mss * 2 )
			tcp_out ( link, TCP_ACK, link.next, nullptr, 0 );

	}

	void nat::fill ( tcp_link& link ) {

		if ( link.local ) {

			while ( local && link.state == LINK_OPEN && !link.host_eof && link.out_size < out_capacity ) {

				uint32_t got = local->produce ( link.outgoing + link.out_size, out_capacity - link.out_size );

				if ( !got )
					break;

				link.out_size += got;

			}

			return;

		}

		while ( link.state != LINK_FREE && link.readable && !link.host_eof && link.out_size < out_capacity ) {

			int got = recv ( link.socket, reinterpret_cast < char* > ( link.outgoing + link.out_size ), static_cast < int > ( out_capacity - link.out_size ), 0 );

			if ( got == SOCKET_ERROR ) {

				if ( WSAGetLastError ( ) == WSAEWOULDBLOCK )
					link.readable = false;
				else
					close_link ( link, true );

				return;

			}

			if ( got == 0 ) {

				link.host_eof = true;
				link.readable = false;

				return;

			}

			link.out_size += static_cast < uint32_t > ( got );

		}

	}

	void nat::pump ( tcp_link& link ) {

		if ( link.state != LINK_OPEN )
			return;

		uint32_t flight = link.next - link.acked;
		uint32_t sent = flight < link.out_size ? flight : link.out_size;
		bool idle = flight == 0;

		while ( sent < link.out_size ) {

			uint32_t allowed = link.window > flight ? link.window - flight : 0;
			uint32_t chunk = link.out_size - sent;

			if ( chunk > link.mss )
				chunk = link.mss;

			if ( chunk > allowed )
				chunk = allowed;

			if ( !chunk )
				break;

			tcp_out ( link, TCP_ACK | ( sent + chunk == link.out_size ? TCP_PSH : 0 ), link.acked + sent, link.outgoing + sent, chunk );
			sent += chunk;
			flight += chunk;
			link.next = link.acked + sent;

		}

		if ( link.host_eof && !link.fin_sent && !link.fin_acked && sent == link.out_size ) {

			tcp_out ( link, TCP_FIN | TCP_ACK, link.acked + link.out_size, nullptr, 0 );
			link.fin_sent = true;
			link.next = link.acked + link.out_size + 1;

		}

		if ( idle && link.next != link.acked )
			link.sent_at = GetTickCount64 ( );

	}

	void nat::service_links ( uint64_t now ) {

		for ( uint32_t i = 0; i < max_links; i++ ) {

			tcp_link& link = links [ i ];

			if ( link.state == LINK_FREE )
				continue;

			WSANETWORKEVENTS events = { };

			if ( !link.local && WSAEnumNetworkEvents ( link.socket, nullptr, &events ) == 0 ) {

				if ( events.lNetworkEvents & FD_CONNECT ) {

					if ( events.iErrorCode [ FD_CONNECT_BIT ] ) {

						InterlockedIncrement ( &connect_failures );
						tcp_reset ( link.remote, link.remote_port, guest_address, link.guest_port, 0, link.guest_next );
						close_link ( link, false );

						continue;

					}

					link.writable = true;

					if ( link.state == LINK_CONNECTING && link.proxy_stage == 1 ) {

						static const char hello [ 3 ] = { 5, 1, 0 };

						if ( ::send ( link.socket, hello, 3, 0 ) != 3 ) {

							tcp_reset ( link.remote, link.remote_port, guest_address, link.guest_port, 0, link.guest_next );
							close_link ( link, false );

							continue;

						}

						link.proxy_stage = 2;

					} else if ( link.state == LINK_CONNECTING ) {

						link.state = LINK_ANSWERED;
						link.sent_at = now;
						tcp_out ( link, TCP_SYN | TCP_ACK, link.start, nullptr, 0 );

					}

				}

				if ( events.lNetworkEvents & FD_READ )
					link.readable = true;

				if ( events.lNetworkEvents & FD_WRITE )
					link.writable = true;

				if ( events.lNetworkEvents & FD_CLOSE )
					link.readable = true;

			}

			if ( link.state == LINK_CONNECTING && link.proxy_stage >= 2 && link.readable ) {

				proxy_step ( link );

				if ( link.state == LINK_FREE )
					continue;

			}

			if ( link.state == LINK_CONNECTING && now - link.sent_at > 30000 ) {

				tcp_reset ( link.remote, link.remote_port, guest_address, link.guest_port, 0, link.guest_next );
				close_link ( link, false );

				continue;

			}

			if ( link.state == LINK_ANSWERED && now - link.sent_at > 1000 ) {

				if ( ++link.retries > 6 ) {

					InterlockedIncrement ( &unanswered );
					close_link ( link, true );

					continue;

				}

				link.sent_at = now;
				tcp_out ( link, TCP_SYN | TCP_ACK, link.start, nullptr, 0 );

			}

			if ( link.state != LINK_OPEN )
				continue;

			flush ( link );
			fill ( link );

			if ( link.state == LINK_FREE )
				continue;

			if ( link.next != link.acked && now - link.sent_at > ( 250ull << ( link.retries < 5 ? link.retries : 5 ) ) ) {

				if ( ++link.retries > 12 ) {

					close_link ( link, true );

					continue;

				}

				link.next = link.acked;
				link.fin_sent = false;
				link.sent_at = now;

			}

			pump ( link );

			if ( link.state != LINK_FREE && link.guest_fin && link.fin_acked && !link.in_size )
				close_link ( link, false );

		}

	}

}