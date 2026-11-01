#pragma once

#include "base/base.hh"

namespace machine {

	enum gpr : uint8_t {

		REG_AX,
		REG_CX,
		REG_DX,
		REG_BX,
		REG_SP,
		REG_BP,
		REG_SI,
		REG_DI,

	};

	enum segment : uint8_t {

		SEG_ES,
		SEG_CS,
		SEG_SS,
		SEG_DS,

	};

	enum flag : uint16_t {

		FLAG_CARRY = 0x0001,
		FLAG_PARITY = 0x0004,
		FLAG_AUXILIARY = 0x0010,
		FLAG_ZERO = 0x0040,
		FLAG_SIGN = 0x0080,
		FLAG_TRAP = 0x0100,
		FLAG_INTERRUPT = 0x0200,
		FLAG_DIRECTION = 0x0400,
		FLAG_OVERFLOW = 0x0800,
		FLAG_FIXED = 0xF002,

	};

	struct registers {

		uint16_t general [ 8 ] = { };
		uint16_t segments [ 4 ] = { };
		uint16_t ip = 0;
		uint16_t flags = FLAG_FIXED;

		uint8_t low ( gpr which ) const {

			return static_cast < uint8_t > ( general [ which ] );

		}

		uint8_t high ( gpr which ) const {

			return static_cast < uint8_t > ( general [ which ] >> 8 );

		}

		bool test ( flag which ) const {

			return ( flags & which ) != 0;

		}

	};

}