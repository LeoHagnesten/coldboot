#pragma once

#include "base/base.hh"

namespace tests {

	struct program {

		uint8_t bytes [ 0x2000 ] = { };
		uint32_t size = 0;

		uint32_t here ( ) const {

			return size;

		}

		program& emit ( uint8_t value ) {

			bytes [ size++ ] = value;

			return *this;

		}

		program& word ( uint16_t value ) {

			return emit ( static_cast < uint8_t > ( value ) ).emit ( static_cast < uint8_t > ( value >> 8 ) );

		}

		program& mov_ax ( uint16_t value ) {

			return emit ( 0xB8 ).word ( value );

		}

		program& mov_al ( uint8_t value ) {

			return emit ( 0xB0 ).emit ( value );

		}

		program& mov_dx ( uint16_t value ) {

			return emit ( 0xBA ).word ( value );

		}

		program& out ( uint8_t port ) {

			return emit ( 0xE6 ).emit ( port );

		}

		program& out_byte ( uint8_t port, uint8_t value ) {

			return mov_al ( value ).out ( port );

		}

		program& store ( uint16_t offset, uint16_t value ) {

			return emit ( 0xC7 ).emit ( 0x06 ).word ( offset ).word ( value );

		}

		program& store_byte ( uint16_t offset, uint8_t value ) {

			return emit ( 0xC6 ).emit ( 0x06 ).word ( offset ).emit ( value );

		}

		program& crtc ( uint8_t index, uint8_t value ) {

			return mov_dx ( 0x3D4 ).mov_al ( index ).emit ( 0xEE ).emit ( 0x42 ).mov_al ( value ).emit ( 0xEE );

		}

		program& branch ( uint8_t opcode, uint32_t target ) {

			return emit ( opcode ).emit ( static_cast < uint8_t > ( target - ( size + 1 ) ) );

		}

		program& call ( uint32_t target ) {

			return emit ( 0xE8 ).word ( static_cast < uint16_t > ( target - ( size + 2 ) ) );

		}

	};

}