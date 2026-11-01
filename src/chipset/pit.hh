#pragma once

#include "bus/bus.hh"

namespace machine {

	struct pit_channel {

		uint32_t count = 0;
		uint32_t reload = 0x10000;
		uint16_t written = 0;
		uint16_t latch = 0;
		uint8_t mode = 0;
		uint8_t access = 3;
		bool bcd = false;
		bool output = true;
		bool gate = true;
		bool counting = false;
		bool armed = false;
		bool load_pending = false;
		bool trigger_pending = false;
		bool odd_wait = false;
		bool latched = false;
		bool write_high = false;
		bool read_high = false;
		bool latch_high = false;

		void clock ( );

		void load ( );

		uint16_t visible ( ) const;

	};

	class pit : public port_device {

	public:

		void reset ( );

		void clock ( );

		void set_gate ( int32_t index, bool high );

		uint8_t in ( uint16_t port ) override;

		void out ( uint16_t port, uint8_t value ) override;

		pit_channel channels [ 3 ];

	};

}