#pragma once

#include "base/base.hh"

namespace machine {

	class archive;

	class keyboard {

	public:

		static constexpr uint32_t queue_size = 64;

		void reset ( );

		void press ( uint8_t scan );

		void advance ( uint32_t cycles );

		void set_clock ( bool high );

		void set_clear ( bool high );

		void transfer ( archive& io );

		uint8_t data = 0;
		bool full = false;

	private:

		uint8_t queue [ queue_size ] = { };
		uint32_t head = 0;
		uint32_t count = 0;
		uint64_t now = 0;
		uint64_t clock_low_since = 0;
		uint32_t wait = 0;
		bool clock_high = true;
		bool clear_high = false;

	};

}