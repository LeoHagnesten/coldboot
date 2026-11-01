#pragma once

#include "hv/machine.hh"

namespace pc {

	class ioapic : public hv::device, public hv::eoi_listener {

	public:

		static constexpr uint64_t base = 0xFEC00000;
		static constexpr uint64_t window = 0x1000;
		static constexpr uint32_t pins = 24;

		explicit ioapic ( hv::machine& owner );

		void reset ( );

		uint64_t read ( uint64_t address, uint32_t size ) override;

		void write ( uint64_t address, uint32_t size, uint64_t value ) override;

		void eoi ( uint32_t vector ) override;

		void set_line ( uint32_t pin, bool asserted );

		uint64_t entry ( uint32_t pin );

	private:

		uint32_t read_register ( uint32_t index ) const;

		void write_register ( uint32_t index, uint32_t value );

		void deliver ( uint32_t pin );

		void check ( uint32_t pin );

		hv::machine& machine;
		SRWLOCK lock = SRWLOCK_INIT;
		uint64_t table [ pins ] = { };
		uint32_t lines = 0;
		uint32_t select = 0;
		uint32_t id = 0;

	};

}