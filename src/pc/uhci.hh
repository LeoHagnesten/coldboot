#pragma once

#include "pc/pci.hh"
#include "pc/usb.hh"

namespace pc {

	class uhci : public hv::device, public pci_function, public ticking {

	public:

		static constexpr uint16_t default_base = 0xC040;
		static constexpr int32_t port_count = 2;

		uhci ( platform_bus& owner, hv::machine& target, uint32_t line_number );

		void reset ( );

		void plug ( int32_t port, usb_device* device );

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		void written ( uint32_t offset ) override;

		void advance ( uint64_t now ) override;

		void run_frame ( );

		uint16_t base = default_base;
		uint32_t frames_run = 0;

	private:

		enum td_result : uint8_t {

			TD_SKIPPED,
			TD_WAITING,
			TD_DONE,
			TD_HALTED,

		};

		uint16_t read_word ( uint32_t offset ) const;

		void write_word ( uint32_t offset, uint16_t value, uint16_t mask );

		uint32_t read32 ( uint32_t address );

		void write32 ( uint32_t address, uint32_t value );

		td_result run_td ( uint32_t address );

		usb_device* find ( uint8_t address );

		void reset_controller ( );

		void update_line ( );

		platform_bus& bus;
		hv::machine& vm;
		uint32_t irq = 16;
		uint16_t command = 0;
		uint16_t status = 0x20;
		uint16_t enabled = 0;
		uint16_t frame = 0;
		uint32_t list = 0;
		uint8_t sof = 0x40;
		uint16_t ports [ port_count ] = { };
		usb_device* devices [ port_count ] = { };
		uint64_t last_frame = 0;
		bool line = false;
		bool completed = false;
		bool shorted = false;
		bool failed = false;
		bool global_reset = false;
		uint8_t scratch [ 1280 ] = { };

	};

}