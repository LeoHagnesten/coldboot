#pragma once

#include "bus/bus.hh"
#include "cpu/registers.hh"

namespace machine {

	struct interrupt_line {

		virtual bool pending ( ) = 0;

		virtual uint8_t acknowledge ( ) = 0;

	};

	class cpu {

	public:

		explicit cpu ( bus& memory ) : system ( memory ) {

		}

		void reset ( );

		void step ( );

		void interrupt ( uint8_t vector );

		void transfer ( archive& io );

		bool interrupt_waiting ( ) const {

			return nmi || ( ( state.flags & FLAG_INTERRUPT ) && controller && controller->pending ( ) );

		}

		static uint32_t linear ( uint16_t segment_value, uint16_t offset ) {

			return ( static_cast < uint32_t > ( segment_value ) << 4 ) + offset;

		}

		uint32_t instruction_address ( ) const {

			return linear ( state.segments [ SEG_CS ], state.ip );

		}

		registers state;
		bus& system;
		interrupt_line* controller = nullptr;
		uint64_t cycles = 0;
		uint64_t instructions = 0;
		uint32_t queue = 0;
		bool halted = false;
		bool nmi = false;
		bool inhibit = false;
		bool vectored = false;

	};

}