#pragma once

#include "bus/bus.hh"
#include "input/keyboard.hh"
#include "chipset/pit.hh"

namespace machine {

	class ppi : public port_device {

	public:

		ppi ( keyboard& keys, pit& timer ) : keys ( keys ), timer ( timer ) {

		}

		void reset ( );

		bool speaker ( ) const {

			return ( port_b & 0x02 ) && timer.channels [ 2 ].output;

		}

		uint8_t in ( uint16_t port ) override;

		void out ( uint16_t port, uint8_t value ) override;

		void transfer ( archive& io );

		uint8_t port_b = 0;
		uint8_t switches = 0x2D;

	private:

		keyboard& keys;
		pit& timer;

	};

}