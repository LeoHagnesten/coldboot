#include "cpu/cpu.hh"
#include "machine/archive.hh"

namespace machine {

	void cpu::transfer ( archive& io ) {

		io.field ( state );
		io.field ( cycles );
		io.field ( instructions );
		io.field ( queue );
		io.field ( halted );
		io.field ( nmi );
		io.field ( inhibit );
		io.field ( vectored );

	}

	void cpu::reset ( ) {

		state = registers ( );
		state.segments [ SEG_CS ] = 0xFFFF;
		state.ip = 0x0000;
		state.flags = FLAG_FIXED;
		system.a20 = false;
		cycles = 0;
		instructions = 0;
		queue = 0;
		halted = false;
		nmi = false;
		inhibit = false;

	}

	void cpu::interrupt ( uint8_t vector ) {

		registers& r = state;
		auto push_value = [ & ] ( uint16_t value ) {

			r.general [ REG_SP ] = static_cast < uint16_t > ( r.general [ REG_SP ] - 2 );

			uint32_t address = linear ( r.segments [ SEG_SS ], r.general [ REG_SP ] );

			system.write8 ( address, static_cast < uint8_t > ( value ) );
			system.write8 ( linear ( r.segments [ SEG_SS ], static_cast < uint16_t > ( r.general [ REG_SP ] + 1 ) ), static_cast < uint8_t > ( value >> 8 ) );

		};

		push_value ( r.flags );
		r.flags = static_cast < uint16_t > ( r.flags & ~( FLAG_INTERRUPT | FLAG_TRAP ) );
		push_value ( r.segments [ SEG_CS ] );
		push_value ( r.ip );
		r.ip = system.read16 ( static_cast < uint32_t > ( vector ) * 4 );
		r.segments [ SEG_CS ] = system.read16 ( static_cast < uint32_t > ( vector ) * 4 + 2 );
		halted = false;
		vectored = true;
		queue = 0;

	}

}