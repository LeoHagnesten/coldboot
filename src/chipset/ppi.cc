#include "chipset/ppi.hh"
#include "machine/archive.hh"

namespace machine {

	void ppi::transfer ( archive& io ) {

		io.field ( port_b );
		io.field ( switches );

	}

	void ppi::reset ( ) {

		out ( 0x61, 0 );

	}

	uint8_t ppi::in ( uint16_t port ) {

		switch ( port & 3 ) {

			case 0:
				return keys.data;

			case 1:
				return port_b;

			case 2: {

				uint8_t nibble = static_cast < uint8_t > ( port_b & 0x08 ? switches >> 4 : switches & 0x0F );

				return static_cast < uint8_t > ( nibble | ( timer.channels [ 2 ].output ? 0x20 : 0 ) );

			}

			default:
				return 0xFF;

		}

	}

	void ppi::out ( uint16_t port, uint8_t value ) {

		if ( ( port & 3 ) != 1 )
			return;

		port_b = value;
		timer.set_gate ( 2, ( value & 0x01 ) != 0 );
		keys.set_clock ( ( value & 0x40 ) != 0 );
		keys.set_clear ( ( value & 0x80 ) != 0 );

	}

}