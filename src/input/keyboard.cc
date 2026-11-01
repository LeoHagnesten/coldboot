#include "input/keyboard.hh"
#include "machine/archive.hh"

namespace machine {

	void keyboard::transfer ( archive& io ) {

		io.field ( data );
		io.field ( full );
		io.field ( queue );
		io.field ( head );
		io.field ( count );
		io.field ( now );
		io.field ( clock_low_since );
		io.field ( wait );
		io.field ( clock_high );
		io.field ( clear_high );

		if ( head >= queue_size || count > queue_size ) {

			head = 0;
			count = 0;
			io.failed = true;

		}

	}

	static constexpr uint32_t reset_hold = 47727;
	static constexpr uint32_t byte_gap = 4772;

	void keyboard::reset ( ) {

		data = 0;
		full = false;
		head = 0;
		count = 0;
		now = 0;
		clock_low_since = 0;
		wait = 0;
		clock_high = true;
		clear_high = false;

	}

	void keyboard::press ( uint8_t scan ) {

		if ( count == queue_size )
			return;

		queue [ ( head + count ) % queue_size ] = scan;
		count++;

	}

	void keyboard::advance ( uint32_t cycles ) {

		now += cycles;
		wait = wait > cycles ? wait - cycles : 0;

		if ( full || !count || wait || !clock_high || clear_high )
			return;

		data = queue [ head ];
		head = ( head + 1 ) % queue_size;
		count--;
		full = true;

	}

	void keyboard::set_clock ( bool high ) {

		if ( high == clock_high )
			return;

		clock_high = high;

		if ( !high ) {

			clock_low_since = now;

			return;

		}

		if ( now - clock_low_since < reset_hold )
			return;

		head = 0;
		count = 0;
		full = false;
		press ( 0xAA );
		wait = byte_gap;

	}

	void keyboard::set_clear ( bool high ) {

		if ( high == clear_high )
			return;

		clear_high = high;

		if ( high ) {

			data = 0;
			full = false;
			wait = byte_gap;

		}

	}

}