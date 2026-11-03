#pragma once

#include "net/link.hh"

namespace net {

	struct tcp_link;
	struct udp_flow;
	struct echo_request;

	class nat : public wire {

	public:

		static constexpr uint32_t guest_address = 0x0A00020F;
		static constexpr uint32_t gateway_address = 0x0A000202;
		static constexpr uint32_t resolver_address = 0x0A000203;
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

		void service ( );

		volatile long frames_in = 0;
		volatile long frames_out = 0;
		volatile long dropped = 0;
		uint8_t guest_mac [ 6 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
		bool guest_known = false;

	private:

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
		uint16_t identity = 1;
		uint32_t sequence_seed = 0x1000;
		bool sockets_ready = false;

	};

}