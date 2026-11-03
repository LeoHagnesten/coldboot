#ifdef COLDBOOT_DEBUG

#include <winsock2.h>
#include <ws2tcpip.h>
#include "net/nat.hh"
#include "pc/e1000.hh"
#include "tests/harness.hh"

namespace tests {

	struct line_bus : pc::platform_bus {

		bool lines [ 24 ] = { };

		void set_irq ( uint32_t irq, bool level ) override {

			if ( irq < 24 )
				lines [ irq ] = level;

		}

		void request_reset ( ) override { }

		void request_power_off ( ) override { }

		void console ( char ) override { }

		uint64_t now ( ) override {

			return 0;

		}

	};

	struct frame_catcher : net::wire, net::port {

		static constexpr uint32_t room = 64;

		uint8_t frames [ room ] [ 1600 ] = { };
		uint32_t sizes [ room ] = { };
		volatile long count = 0;
		SRWLOCK lock = SRWLOCK_INIT;

		void keep ( const uint8_t* frame, uint32_t size ) {

			AcquireSRWLockExclusive ( &lock );

			if ( count < static_cast < long > ( room ) && size <= 1600 ) {

				for ( uint32_t i = 0; i < size; i++ )
					frames [ count ] [ i ] = frame [ i ];

				sizes [ count ] = size;
				InterlockedIncrement ( &count );

			}

			ReleaseSRWLockExclusive ( &lock );

		}

		void send ( const uint8_t* frame, uint32_t size ) override {

			keep ( frame, size );

		}

		void receive ( const uint8_t* frame, uint32_t size ) override {

			keep ( frame, size );

		}

		bool wait ( long wanted, uint32_t milliseconds ) {

			for ( uint32_t waited = 0; count < wanted && waited < milliseconds; waited += 5 )
				Sleep ( 5 );

			return count >= wanted;

		}

	};

	static uint16_t get16 ( const uint8_t* at ) {

		return static_cast < uint16_t > ( at [ 0 ] << 8 | at [ 1 ] );

	}

	static uint32_t get_be32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) << 24 | static_cast < uint32_t > ( at [ 1 ] ) << 16 | static_cast < uint32_t > ( at [ 2 ] ) << 8 | at [ 3 ];

	}

	static void set16 ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value >> 8 );
		at [ 1 ] = static_cast < uint8_t > ( value );

	}

	static void set32 ( uint8_t* at, uint32_t value ) {

		set16 ( at, value >> 16 );
		set16 ( at + 2, value );

	}

	static void store32 ( uint8_t* memory, uint32_t at, uint32_t value ) {

		for ( int32_t i = 0; i < 4; i++ )
			memory [ at + i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

	}

	static const uint8_t guest_mac [ 6 ] = { 0x02, 0xCB, 0x11, 0x22, 0x33, 0x45 };

	static uint32_t guest_packet ( uint8_t* frame, uint8_t protocol, uint32_t source, uint32_t target, const uint8_t* body, uint32_t size ) {

		for ( int32_t i = 0; i < 6; i++ ) {

			frame [ i ] = 0xFF;
			frame [ 6 + i ] = guest_mac [ i ];

		}

		set16 ( frame + 12, 0x0800 );

		uint8_t* ip = frame + 14;

		ip [ 0 ] = 0x45;
		ip [ 1 ] = 0;
		set16 ( ip + 2, 20 + size );
		set16 ( ip + 4, 7 );
		set16 ( ip + 6, 0 );
		ip [ 8 ] = 64;
		ip [ 9 ] = protocol;
		set16 ( ip + 10, 0 );
		set32 ( ip + 12, source );
		set32 ( ip + 16, target );
		set16 ( ip + 10, net::fold ( net::sum_words ( ip, 20, 0 ) ) );

		for ( uint32_t i = 0; i < size; i++ )
			ip [ 20 + i ] = body [ i ];

		return 34 + size;

	}

	static uint32_t guest_tcp ( uint8_t* frame, uint16_t source_port, uint32_t target, uint16_t target_port, uint32_t sequence, uint32_t acknowledge, uint8_t flags, const char* text ) {

		uint8_t body [ 128 ] = { };
		uint32_t length = 0;

		while ( text && text [ length ] )
			length++;

		set16 ( body, source_port );
		set16 ( body + 2, target_port );
		set32 ( body + 4, sequence );
		set32 ( body + 8, acknowledge );
		body [ 12 ] = 0x50;
		body [ 13 ] = flags;
		set16 ( body + 14, 0xFFFF );

		for ( uint32_t i = 0; i < length; i++ )
			body [ 20 + i ] = static_cast < uint8_t > ( text [ i ] );

		return guest_packet ( frame, 6, net::nat::guest_address, target, body, 20 + length );

	}

	static void nic_tests ( ) {

		line_bus* bus = new line_bus ( );
		hv::machine* vm = new hv::machine ( );

		if ( !hv::present ( ) || !vm->create ( 1, 2 * 1024 * 1024 ) ) {

			delete vm;
			delete bus;

			return;

		}

		frame_catcher* catcher = new frame_catcher ( );
		pc::e1000* nic = new pc::e1000 ( *bus, *vm, 19, guest_mac );
		uint8_t* memory = vm->memory ( );
		uint64_t base = nic->memory_base;

		nic->connect ( catcher );

		uint16_t sum = 0;
		bool loaded = true;

		for ( uint32_t i = 0; i < 64; i++ ) {

			nic->write ( base + 0x14, 4, i << 8 | 1 );

			uint32_t value = static_cast < uint32_t > ( nic->read ( base + 0x14, 4 ) );

			loaded = loaded && ( value & 0x10 );
			sum = static_cast < uint16_t > ( sum + ( value >> 16 ) );

			if ( i == 0 )
				loaded = loaded && ( value >> 16 ) == 0xCB02;

		}

		uint32_t word = 0;

		nic->write ( base + 0x10, 4, 0x02 );

		for ( int32_t bit = 8; bit >= 0; bit-- ) {

			uint32_t data = ( ( 0x181u >> bit ) & 1 ) << 2;

			nic->write ( base + 0x10, 4, 0x02 | data );
			nic->write ( base + 0x10, 4, 0x03 | data );
			nic->write ( base + 0x10, 4, 0x02 | data );

		}

		for ( int32_t bit = 0; bit < 16; bit++ ) {

			nic->write ( base + 0x10, 4, 0x03 );
			word = word << 1 | ( nic->read ( base + 0x10, 4 ) >> 3 & 1 );
			nic->write ( base + 0x10, 4, 0x02 );

		}

		nic->write ( base + 0x10, 4, 0x00 );
		expect ( loaded && sum == 0xBABA && word == 0x2211, "network: the e1000 EEPROM reads through EERD and Microwire with the MAC and a 0xBABA checksum" );

		static const uint8_t outgoing [ 60 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0xCB, 0x11, 0x22, 0x33, 0x45, 0x08, 0x06, 1, 2, 3 };

		for ( uint32_t i = 0; i < 60; i++ )
			memory [ 0x11000 + i ] = outgoing [ i ];

		store32 ( memory, 0x10000, 0x11000 );
		store32 ( memory, 0x10004, 0 );
		store32 ( memory, 0x10008, 60 | 0x09u << 24 );
		store32 ( memory, 0x1000C, 0 );
		nic->write ( base + 0x3800, 4, 0x10000 );
		nic->write ( base + 0x3804, 4, 0 );
		nic->write ( base + 0x3808, 4, 128 );
		nic->write ( base + 0x3810, 4, 0 );
		nic->write ( base + 0x3818, 4, 0 );
		nic->write ( base + 0x00D0, 4, 0x81 );
		nic->read ( base + 0x00C0, 4 );
		nic->write ( base + 0x0400, 4, 0x02 );
		nic->write ( base + 0x3818, 4, 1 );

		bool sent = catcher->count == 1 && catcher->sizes [ 0 ] == 60 && catcher->frames [ 0 ] [ 14 ] == 1 && ( memory [ 0x1000C ] & 1 ) && nic->read ( base + 0x3810, 4 ) == 1 && bus->lines [ 19 ];
		uint32_t cause = static_cast < uint32_t > ( nic->read ( base + 0x00C0, 4 ) );

		expect ( sent && ( cause & 1 ) && !bus->lines [ 19 ], "network: the e1000 sends a legacy descriptor, writes DD back, and raises TXDW until ICR is read" );

		for ( uint32_t i = 0; i < 8; i++ ) {

			store32 ( memory, 0x12000 + i * 16, 0x13000 + i * 2048 );
			store32 ( memory, 0x12004 + i * 16, 0 );
			store32 ( memory, 0x12008 + i * 16, 0 );
			store32 ( memory, 0x1200C + i * 16, 0 );

		}

		nic->write ( base + 0x2800, 4, 0x12000 );
		nic->write ( base + 0x2804, 4, 0 );
		nic->write ( base + 0x2808, 4, 128 );
		nic->write ( base + 0x2810, 4, 0 );
		nic->write ( base + 0x2818, 4, 7 );
		nic->write ( base + 0x0100, 4, 0x02 | 1u << 15 | 1u << 26 );

		uint8_t incoming [ 42 ] = { 0x02, 0xCB, 0x11, 0x22, 0x33, 0x45, 0x02, 0xCB, 0, 0, 0, 2, 0x08, 0x06, 0, 1 };

		nic->receive ( incoming, 42 );

		bool landed = memory [ 0x13000 ] == 0x02 && memory [ 0x13000 + 15 ] == 1 && memory [ 0x12008 ] == 60 && ( memory [ 0x1200C ] & 0x03 ) == 0x03 && nic->read ( base + 0x2810, 4 ) == 1 && bus->lines [ 19 ];

		nic->read ( base + 0x00C0, 4 );
		nic->write ( base + 0x0100, 4, 0x02 | 1u << 15 );
		nic->receive ( incoming, 42 );

		bool crc = memory [ 0x12018 ] == 64 && nic->read ( base + 0x2810, 4 ) == 2;

		expect ( landed && crc, "network: the e1000 receives into the ring, pads to 60 bytes, sets DD and EOP, and appends the CRC unless stripped" );

		uint8_t* frame = memory + 0x14000;
		uint32_t payload = 2500;

		for ( uint32_t i = 0; i < 54 + payload; i++ )
			frame [ i ] = static_cast < uint8_t > ( i * 7 );

		set16 ( frame + 12, 0x0800 );
		frame [ 14 ] = 0x45;
		frame [ 23 ] = 6;
		set16 ( frame + 18, 0x1000 );
		set16 ( frame + 24, 0 );
		set32 ( frame + 38, 5000 );
		frame [ 46 ] = 0x50;
		frame [ 47 ] = 0x19;
		catcher->count = 0;

		uint32_t ring = 0x10000 + 16;

		store32 ( memory, ring, 14 | 24 << 8 | 33 << 16 );
		store32 ( memory, ring + 4, 34 | 50 << 8 );
		store32 ( memory, ring + 8, payload | 0u << 20 | 0x2Fu << 24 );
		store32 ( memory, ring + 12, 54u << 8 | 1000u << 16 );
		store32 ( memory, ring + 16, 0x14000 );
		store32 ( memory, ring + 20, 0 );
		store32 ( memory, ring + 24, ( 54 + payload ) | 1u << 20 | 0x2Du << 24 );
		store32 ( memory, ring + 28, 0x03u << 8 );
		nic->write ( base + 0x3818, 4, 3 );

		bool split = catcher->count == 3 && catcher->sizes [ 0 ] == 1054 && catcher->sizes [ 1 ] == 1054 && catcher->sizes [ 2 ] == 554;

		for ( long i = 0; split && i < 3; i++ ) {

			const uint8_t* piece = catcher->frames [ i ];
			uint32_t length = get16 ( piece + 16 );
			uint32_t pseudo = net::sum_words ( piece + 26, 8, 0 ) + 6 + ( length - 20 );

			split = split && length == catcher->sizes [ i ] - 14 && get_be32 ( piece + 38 ) == 5000 + 1000 * static_cast < uint32_t > ( i ) && get16 ( piece + 18 ) == 0x1000 + i;
			split = split && net::fold ( net::sum_words ( piece + 14, 20, 0 ) ) == 0 && net::fold ( net::sum_words ( piece + 34, length - 20, pseudo ) ) == 0;
			split = split && ( ( piece [ 47 ] & 0x09 ) != 0 ) == ( i == 2 );

		}

		expect ( split, "network: the e1000 splits a TSO send into MSS segments with fixed lengths, IDs, sequence numbers and checksums" );

		delete nic;
		delete catcher;
		delete vm;
		delete bus;

	}

	static void nat_tests ( ) {

		frame_catcher* catcher = new frame_catcher ( );
		net::nat* router = new net::nat ( );
		uint8_t frame [ 1600 ];

		if ( !router->start ( catcher ) ) {

			expect ( false, "network: the NAT starts" );
			delete router;
			delete catcher;

			return;

		}

		uint8_t arp [ 42 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0xCB, 0x11, 0x22, 0x33, 0x45, 0x08, 0x06, 0, 1, 0x08, 0, 6, 4, 0, 1, 0x02, 0xCB, 0x11, 0x22, 0x33, 0x45, 10, 0, 2, 15, 0, 0, 0, 0, 0, 0, 10, 0, 2, 2 };

		router->send ( arp, 42 );

		bool resolved = catcher->wait ( 1, 2000 ) && get16 ( catcher->frames [ 0 ] + 12 ) == 0x0806 && get16 ( catcher->frames [ 0 ] + 20 ) == 2 && get_be32 ( catcher->frames [ 0 ] + 28 ) == net::nat::gateway_address && catcher->frames [ 0 ] [ 0 ] == 0x02 && catcher->frames [ 0 ] [ 5 ] == 0x45;

		uint8_t bootp [ 8 + 244 ] = { };

		set16 ( bootp, 68 );
		set16 ( bootp + 2, 67 );
		set16 ( bootp + 4, sizeof ( bootp ) );

		uint8_t* request = bootp + 8;

		request [ 0 ] = 1;
		request [ 1 ] = 1;
		request [ 2 ] = 6;
		set32 ( request + 4, 0x12345678 );

		for ( int32_t i = 0; i < 6; i++ )
			request [ 28 + i ] = guest_mac [ i ];

		set32 ( request + 236, 0x63825363 );
		request [ 240 ] = 53;
		request [ 241 ] = 1;
		request [ 242 ] = 1;
		request [ 243 ] = 255;
		router->send ( frame, guest_packet ( frame, 17, 0, 0xFFFFFFFFu, bootp, sizeof ( bootp ) ) );

		const uint8_t* offer = catcher->wait ( 2, 2000 ) ? catcher->frames [ 1 ] : nullptr;
		bool leased = offer && offer [ 23 ] == 17 && get16 ( offer + 36 ) == 68 && get_be32 ( offer + 42 + 4 ) == 0x12345678 && get_be32 ( offer + 42 + 16 ) == net::nat::guest_address && offer [ 42 + 240 ] == 53 && offer [ 42 + 242 ] == 2;

		expect ( resolved && leased, "network: the NAT answers ARP for the gateway and offers 10.0.2.15 over DHCP" );

		uint8_t echo [ 12 ] = { 8, 0, 0, 0, 0x12, 0x34, 0, 1, 'p', 'i', 'n', 'g' };

		set16 ( echo + 2, net::fold ( net::sum_words ( echo, 12, 0 ) ) );
		router->send ( frame, guest_packet ( frame, 1, net::nat::guest_address, net::nat::gateway_address, echo, 12 ) );

		const uint8_t* pong = catcher->wait ( 3, 2000 ) ? catcher->frames [ 2 ] : nullptr;

		expect ( pong && pong [ 23 ] == 1 && pong [ 34 ] == 0 && get16 ( pong + 38 ) == 0x1234 && pong [ 42 ] == 'p' && net::fold ( net::sum_words ( pong + 34, 12, 0 ) ) == 0, "network: the NAT answers a ping to the gateway" );

		SOCKET listener = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		sockaddr_in place = { };
		int place_length = sizeof ( place );

		place.sin_family = AF_INET;
		place.sin_addr.s_addr = htonl ( 0x7F000001 );
		bind ( listener, reinterpret_cast < sockaddr* > ( &place ), sizeof ( place ) );
		listen ( listener, 1 );
		getsockname ( listener, reinterpret_cast < sockaddr* > ( &place ), &place_length );

		uint16_t number = ntohs ( place.sin_port );

		catcher->count = 0;
		router->send ( frame, guest_tcp ( frame, 40000, net::nat::gateway_address, number, 100, 0, 0x02, nullptr ) );

		const uint8_t* answer = catcher->wait ( 1, 3000 ) ? catcher->frames [ 0 ] : nullptr;
		bool syn = answer && answer [ 23 ] == 6 && answer [ 47 ] == 0x12 && get_be32 ( answer + 42 ) == 101;
		uint32_t theirs = answer ? get_be32 ( answer + 38 ) + 1 : 0;
		fd_set ready;
		timeval patience = { 2, 0 };

		FD_ZERO ( &ready );
		FD_SET ( listener, &ready );

		SOCKET peer = select ( 0, &ready, nullptr, nullptr, &patience ) == 1 ? accept ( listener, nullptr, nullptr ) : INVALID_SOCKET;

		router->send ( frame, guest_tcp ( frame, 40000, net::nat::gateway_address, number, 101, theirs, 0x18, "hello" ) );

		char heard [ 16 ] = { };
		int heard_size = 0;

		if ( peer != INVALID_SOCKET ) {

			DWORD timeout = 2000;

			setsockopt ( peer, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast < const char* > ( &timeout ), sizeof ( timeout ) );
			heard_size = recv ( peer, heard, sizeof ( heard ), 0 );
			::send ( peer, "world", 5, 0 );

		}

		bool forwarded = heard_size == 5 && heard [ 0 ] == 'h' && heard [ 4 ] == 'o';
		bool replied = false;

		for ( uint32_t waited = 0; !replied && waited < 2000; waited += 10 ) {

			Sleep ( 10 );

			for ( long i = 0; i < catcher->count && !replied; i++ ) {

				const uint8_t* piece = catcher->frames [ i ];

				replied = catcher->sizes [ i ] >= 59 && piece [ 23 ] == 6 && piece [ 54 ] == 'w' && piece [ 58 ] == 'd' && get_be32 ( piece + 38 ) == theirs && get_be32 ( piece + 42 ) == 106;

			}

		}

		expect ( syn && forwarded && replied, "network: the NAT bridges a guest TCP connection to a host socket in both directions" );

		if ( peer != INVALID_SOCKET )
			closesocket ( peer );

		bool finished = false;

		for ( uint32_t waited = 0; !finished && waited < 2000; waited += 10 ) {

			Sleep ( 10 );

			for ( long i = 0; i < catcher->count && !finished; i++ )
				finished = catcher->frames [ i ] [ 23 ] == 6 && ( catcher->frames [ i ] [ 47 ] & 0x01 );

		}

		expect ( finished, "network: the NAT passes a host close to the guest as a FIN" );
		closesocket ( listener );
		router->stop ( );
		delete router;
		delete catcher;

	}

	void network_tests ( ) {

		print ( "\r\n" );
		nic_tests ( );
		nat_tests ( );

	}

}

#endif