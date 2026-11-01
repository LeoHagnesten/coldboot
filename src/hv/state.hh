#pragma once

#include "base/base.hh"

namespace hv {

	enum register_index : uint8_t {

		REG_AX,
		REG_CX,
		REG_DX,
		REG_BX,
		REG_SP,
		REG_BP,
		REG_SI,
		REG_DI,

	};

	enum segment_index : uint8_t {

		SEG_ES,
		SEG_CS,
		SEG_SS,
		SEG_DS,
		SEG_FS,
		SEG_GS,
		SEG_COUNT,

	};

	static constexpr uint64_t FLAG_CF = 0x0001;
	static constexpr uint64_t FLAG_PF = 0x0004;
	static constexpr uint64_t FLAG_AF = 0x0010;
	static constexpr uint64_t FLAG_ZF = 0x0040;
	static constexpr uint64_t FLAG_SF = 0x0080;
	static constexpr uint64_t FLAG_DF = 0x0400;
	static constexpr uint64_t FLAG_OF = 0x0800;
	static constexpr uint64_t FLAG_ARITHMETIC = FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF | FLAG_OF;

	static constexpr uint16_t SEGMENT_LONG = 0x2000;
	static constexpr uint16_t SEGMENT_DEFAULT = 0x4000;

	struct segment {

		uint64_t base = 0;
		uint32_t limit = 0xFFFF;
		uint16_t selector = 0;
		uint16_t attributes = 0x93;

	};

	struct cpu_state {

		uint64_t gpr [ 16 ] = { };
		uint64_t rip = 0;
		uint64_t rflags = 2;
		uint64_t cr0 = 0x10;
		uint64_t cr3 = 0;
		uint64_t cr4 = 0;
		uint64_t efer = 0;
		segment segments [ SEG_COUNT ];
		uint64_t fault_address = 0;
		uint32_t fault_code = 0;

		bool long_mode ( ) const {

			return ( efer & 0x400 ) != 0;

		}

		bool code64 ( ) const {

			return long_mode ( ) && ( segments [ SEG_CS ].attributes & SEGMENT_LONG );

		}

		uint32_t code_bits ( ) const {

			return code64 ( ) ? 64 : ( segments [ SEG_CS ].attributes & SEGMENT_DEFAULT ? 32 : 16 );

		}

	};

	struct guest_bus {

		virtual ~guest_bus ( ) = default;

		virtual void read ( uint64_t address, uint8_t* out, uint32_t size ) = 0;

		virtual void write ( uint64_t address, const uint8_t* data, uint32_t size ) = 0;

		virtual uint32_t in ( uint16_t port, uint32_t size ) = 0;

		virtual void out ( uint16_t port, uint32_t size, uint32_t value ) = 0;

	};

}