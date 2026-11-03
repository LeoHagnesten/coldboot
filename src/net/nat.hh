#pragma once

#include "net/link.hh"

namespace net {

	struct tcp_link;
	struct udp_flow;
	struct echo_request;

	struct service {

		virtual ~service ( ) = default;

		virtual void opened ( ) = 0;

		virtual void received ( const uint8_t* data, uint32_t size ) = 0;

		virtual uint32_t produce ( uint8_t* out, uint32_t room ) = 0;

		virtual void closed ( ) = 0;

	};

	class nat : public wire {

	public:

		static constexpr uint32_t guest_address = 0x0A00020F;
		static constexpr uint32_t gateway_address = 0x0A000202;
		static constexpr uint32_t resolver_address = 0x0A000203;
		static constexpr uint32_t tools_address = 0x0A000204;
		static constexpr uint16_t tools_port = 4000;
		static constexpr uint32_t max_links = 160;
		static constexpr uint32_t max_flows = 96;
		static constexpr uint32_t max_echoes = 8;
		static constexpr uint32_t queue_slots = 256;
		static constexpr uint32_t slot_size = 2048;

		nat ( );

		~nat ( ) override;

		bool start ( port* guest );

		void stop ( );

		void send ( const uint8_t* frame, uint32_t size ) override;

		void process ( const uint8_t* frame, uint32_t size );

		void service_all ( );

		void attach_service ( service* target ) {

			local = target;

		}

		void kick ( );

		volatile long frames_in = 0;
		volatile long frames_out = 0;
		volatile long dropped = 0;
		uint8_t guest_mac [ 6 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
		bool guest_known = false;
		bool isolated = false;
		bool local_network = true;
		char proxy [ 128 ] = { };
		volatile long proxied = 0;
		volatile long blocked = 0;
		volatile long replayed = 0;
		volatile long connects = 0;
		volatile long connect_failures = 0;
		volatile long resets = 0;
		volatile long handshakes = 0;
		volatile long unanswered = 0;
		volatile long guest_resets = 0;

		uint32_t links_in_use ( ) const;

		bool ask_first = false;

		static constexpr uint32_t synthetic_base = 0xC6120000u;
		static constexpr uint32_t name_slots = 1024;
		static constexpr uint32_t max_gates = 256;
		static constexpr uint32_t max_requests = 128;

		static bool private_address ( uint32_t address );

		struct access_request {

			uint32_t id = 0;
			uint8_t protocol = 0;
			uint16_t guest_port = 0;
			uint32_t remote = 0;
			uint16_t remote_port = 0;
			char host [ 256 ] = { };

		};

		bool next_request ( access_request& out );

		void decide ( uint32_t id, bool allow );

	private:

		struct gate_entry {

			uint32_t id = 0;
			uint8_t protocol = 0;
			uint8_t state = 0;
			bool handled = false;
			uint16_t guest_port = 0;
			uint32_t remote = 0;
			uint16_t remote_port = 0;
			uint64_t since = 0;
			uint8_t segment [ 64 ] = { };
			uint32_t segment_size = 0;

		};

		bool gate ( uint8_t protocol, uint16_t guest_port, uint32_t remote, uint16_t remote_port, const uint8_t* segment, uint32_t size, const char* host );

		void service_gates ( uint64_t now );

		void remember_names ( const uint8_t* data, uint32_t size );

		struct name_entry {

			char name [ 256 ];
			uint32_t address;

		};

		void answer_dns ( uint16_t guest_port, const uint8_t* data, uint32_t size );

		uint32_t name_address ( const char* name );

		const char* address_name ( uint32_t address ) const;

		bool resolve_proxy ( );

		void proxy_step ( tcp_link& link );

		static unsigned long __stdcall loop ( void* context );

		void run ( );

		void find_resolver ( );

		void handle_arp ( const uint8_t* frame, uint32_t size );

		void handle_ip ( const uint8_t* packet, uint32_t size );

		void handle_icmp ( uint32_t source, uint32_t target, const uint8_t* data, uint32_t size );

		void handle_udp ( uint32_t source, uint32_t target, const uint8_t* data, uint32_t size );

		void handle_tcp ( uint32_t source, uint32_t target, const uint8_t* data, uint32_t size );

		void dhcp ( const uint8_t* data, uint32_t size );

		void ip_out ( uint8_t protocol, uint32_t source, uint32_t target, const uint8_t* head, uint32_t head_size, const uint8_t* body, uint32_t body_size );

		void udp_out ( uint32_t source, uint16_t source_port, uint32_t target, uint16_t target_port, const uint8_t* data, uint32_t size );

		void tcp_out ( tcp_link& link, uint8_t flags, uint32_t sequence, const uint8_t* data, uint32_t size );

		void tcp_reset ( uint32_t source, uint16_t source_port, uint32_t target, uint16_t target_port, uint32_t sequence, uint32_t acknowledge );

		bool map_target ( uint32_t address, uint16_t number, void* out, int32_t& length );

		uint16_t advertised ( const tcp_link& link ) const;

		void pump ( tcp_link& link );

		void fill ( tcp_link& link );

		void flush ( tcp_link& link );

		void close_link ( tcp_link& link, bool reset );

		void service_links ( uint64_t now );

		void service_flows ( uint64_t now );

		void service_echoes ( );

		port* target = nullptr;
		service* local = nullptr;
		tcp_link* links = nullptr;
		udp_flow* flows = nullptr;
		echo_request* echoes = nullptr;
		void* thread = nullptr;
		void* wake = nullptr;
		void* activity = nullptr;
		void* icmp = nullptr;
		volatile long quit = 0;
		uint8_t* queue = nullptr;
		uint32_t queue_sizes [ queue_slots ] = { };
		uint32_t queue_head = 0;
		uint32_t queue_count = 0;
		void* queue_lock = nullptr;
		uint8_t* scratch = nullptr;
		uint8_t resolver [ 28 ] = { };
		int32_t resolver_length = 0;
		uint8_t proxy_address [ 28 ] = { };
		int32_t proxy_length = 0;
		name_entry* names = nullptr;
		uint32_t name_next = 0;
		gate_entry* gates = nullptr;
		access_request* requests = nullptr;
		uint32_t request_count = 0;
		uint32_t gate_serial = 0;
		void* gate_lock = nullptr;
		uint16_t identity = 1;
		uint32_t sequence_seed = 0x1000;
		bool sockets_ready = false;

	};

}