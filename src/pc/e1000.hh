#pragma once

#include "net/link.hh"
#include "pc/common.hh"
#include "pc/pci.hh"

namespace pc {

	class e1000 : public hv::device, public pci_function, public net::port {

	public:

		static constexpr uint32_t default_memory = 0xE1000000;
		static constexpr uint16_t default_io = 0xC080;
		static constexpr uint32_t memory_size = 0x20000;

		e1000 ( platform_bus& owner, hv::machine& target, uint32_t line_number, const uint8_t* address );

		~e1000 ( ) override;

		void connect ( net::wire* backend ) {

			uplink = backend;

		}

		uint64_t read ( uint64_t address, uint32_t size ) override;

		void write ( uint64_t address, uint32_t size, uint64_t value ) override;

		void written ( uint32_t offset ) override;

		void receive ( const uint8_t* frame, uint32_t size ) override;

		uint32_t memory_base = default_memory;
		uint16_t io_base = default_io;
		uint8_t mac [ 6 ] = { };
		volatile long frames_sent = 0;
		volatile long frames_received = 0;

	private:

		struct offload {

			uint8_t ip_start = 0;
			uint8_t ip_offset = 0;
			uint16_t ip_end = 0;
			uint8_t transport_start = 0;
			uint8_t transport_offset = 0;
			uint16_t transport_end = 0;
			uint32_t payload = 0;
			uint8_t command = 0;
			uint8_t header = 0;
			uint16_t segment = 0;

		};

		void reset ( );

		uint32_t read_register ( uint32_t offset );

		void write_register ( uint32_t offset, uint32_t value );

		void write_eeprom_control ( uint32_t value );

		void phy_access ( uint32_t value );

		void transmit ( );

		void finish_packet ( bool segmented, uint8_t options );

		void emit ( uint8_t* frame, uint32_t size );

		void insert_checksum ( uint8_t* frame, uint32_t size, uint32_t start, uint32_t end, uint32_t at );

		bool deliver ( const uint8_t* frame, uint32_t size );

		void flush_pending ( );

		void raise ( uint32_t cause );

		void update_line ( );

		platform_bus& bus;
		hv::machine& vm;
		net::wire* uplink = nullptr;
		uint32_t irq = 19;
		uint32_t* regs = nullptr;
		uint16_t eeprom [ 64 ] = { };
		uint16_t phy [ 32 ] = { };
		uint32_t eeprom_command = 0;
		uint32_t eeprom_bits = 0;
		uint32_t eeprom_address = 0;
		int32_t eeprom_out = -1;
		bool eeprom_clock = false;
		bool eeprom_output = false;
		uint32_t io_address = 0;
		offload context;
		uint8_t* packet = nullptr;
		uint32_t packet_size = 0;
		uint8_t packet_options = 0;
		bool packet_segmented = false;
		uint8_t* pending = nullptr;
		uint32_t pending_sizes [ 32 ] = { };
		uint32_t pending_head = 0;
		uint32_t pending_count = 0;
		bool line = false;

	};

}