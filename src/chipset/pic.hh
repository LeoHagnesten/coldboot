#pragma once

#include "bus/bus.hh"
#include "cpu/cpu.hh"

namespace machine {

	class pic : public port_device, public interrupt_line {

	public:

		void reset ( );

		void set_line ( uint8_t irq, bool high );

		bool pending ( ) override;

		uint8_t acknowledge ( ) override;

		uint8_t in ( uint16_t port ) override;

		void out ( uint16_t port, uint8_t value ) override;

		void transfer ( archive& io );

		uint8_t irr = 0;
		uint8_t isr = 0;
		uint8_t imr = 0xFF;
		uint8_t vector_base = 0x08;

	private:

		int32_t highest_request ( ) const;

		int32_t highest_service ( ) const;

		uint8_t lines = 0;
		uint8_t lowest = 7;
		uint8_t init_step = 0;
		bool level = false;
		bool single = true;
		bool needs_icw4 = false;
		bool auto_eoi = false;
		bool rotate_on_auto_eoi = false;
		bool special_mask = false;
		bool read_service = false;
		bool poll = false;

	};

}