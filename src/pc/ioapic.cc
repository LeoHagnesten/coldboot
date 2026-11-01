#include "pc/ioapic.hh"

namespace pc {

	static constexpr uint64_t entry_mask = 1ull << 16;
	static constexpr uint64_t entry_level = 1ull << 15;
	static constexpr uint64_t entry_remote = 1ull << 14;
	static constexpr uint64_t entry_logical = 1ull << 11;
	static constexpr uint64_t entry_writable = 0xFF0000000001AFFFull;

	ioapic::ioapic ( hv::machine& owner ) : machine ( owner ) {

		reset ( );

	}

	void ioapic::reset ( ) {

		AcquireSRWLockExclusive ( &lock );

		for ( uint64_t& item : table )
			item = entry_mask;

		lines = 0;
		select = 0;
		id = 0;
		ReleaseSRWLockExclusive ( &lock );

	}

	uint64_t ioapic::read ( uint64_t address, uint32_t ) {

		uint64_t offset = address - base;
		uint64_t value = 0;

		AcquireSRWLockExclusive ( &lock );

		if ( offset == 0x00 )
			value = select;
		else if ( offset == 0x10 )
			value = read_register ( select );

		ReleaseSRWLockExclusive ( &lock );

		return value;

	}

	void ioapic::write ( uint64_t address, uint32_t, uint64_t value ) {

		uint64_t offset = address - base;

		AcquireSRWLockExclusive ( &lock );

		if ( offset == 0x00 )
			select = static_cast < uint32_t > ( value & 0xFF );
		else if ( offset == 0x10 )
			write_register ( select, static_cast < uint32_t > ( value ) );

		ReleaseSRWLockExclusive ( &lock );

	}

	uint32_t ioapic::read_register ( uint32_t index ) const {

		if ( index == 0 || index == 2 )
			return id << 24;

		if ( index == 1 )
			return ( pins - 1 ) << 16 | 0x11;

		if ( index >= 0x10 && index < 0x10 + pins * 2 ) {

			uint64_t item = table [ ( index - 0x10 ) / 2 ];

			return static_cast < uint32_t > ( index & 1 ? item >> 32 : item );

		}

		return 0;

	}

	void ioapic::write_register ( uint32_t index, uint32_t value ) {

		if ( index == 0 ) {

			id = value >> 24 & 0xF;

			return;

		}

		if ( index < 0x10 || index >= 0x10 + pins * 2 )
			return;

		uint32_t pin = ( index - 0x10 ) / 2;
		uint64_t item = table [ pin ];
		uint64_t incoming = index & 1 ? static_cast < uint64_t > ( value ) << 32 | ( item & 0xFFFFFFFF ) : ( item & 0xFFFFFFFF00000000ull ) | value;

		table [ pin ] = ( incoming & entry_writable ) | ( item & ~entry_writable );

		if ( !( table [ pin ] & entry_level ) )
			table [ pin ] &= ~entry_remote;

		check ( pin );

	}

	void ioapic::check ( uint32_t pin ) {

		uint64_t item = table [ pin ];

		if ( !( item & entry_mask ) && ( lines >> pin & 1 ) && ( item & entry_level ) && !( item & entry_remote ) )
			deliver ( pin );

	}

	void ioapic::deliver ( uint32_t pin ) {

		uint64_t item = table [ pin ];
		uint32_t mode = static_cast < uint32_t > ( item >> 8 & 7 );
		bool level = ( item & entry_level ) != 0 && mode == 0;
		uint32_t type = WHvX64InterruptTypeFixed;

		if ( mode == 1 )
			type = WHvX64InterruptTypeLowestPriority;
		else if ( mode == 4 )
			type = WHvX64InterruptTypeNmi;
		else if ( mode == 5 )
			type = WHvX64InterruptTypeInit;
		else if ( mode != 0 )
			return;

		if ( level )
			table [ pin ] |= entry_remote;

		machine.interrupt ( static_cast < uint32_t > ( item >> 56 ), static_cast < uint32_t > ( item & 0xFF ), ( item & entry_logical ) != 0, level, type );

	}

	void ioapic::set_line ( uint32_t pin, bool asserted ) {

		if ( pin >= pins )
			return;

		AcquireSRWLockExclusive ( &lock );

		uint32_t bit = 1u << pin;
		bool was = ( lines & bit ) != 0;
		uint64_t item = table [ pin ];

		lines = asserted ? lines | bit : lines & ~bit;

		if ( asserted && !( item & entry_mask ) ) {

			if ( item & entry_level ) {

				if ( !( item & entry_remote ) )
					deliver ( pin );

			} else if ( !was )
				deliver ( pin );

		}

		ReleaseSRWLockExclusive ( &lock );

	}

	void ioapic::eoi ( uint32_t vector ) {

		AcquireSRWLockExclusive ( &lock );

		for ( uint32_t pin = 0; pin < pins; pin++ ) {

			uint64_t item = table [ pin ];

			if ( ( item & entry_level ) && ( item & entry_remote ) && ( item & 0xFF ) == vector ) {

				table [ pin ] &= ~entry_remote;
				check ( pin );

			}

		}

		ReleaseSRWLockExclusive ( &lock );

	}

	uint64_t ioapic::entry ( uint32_t pin ) {

		AcquireSRWLockExclusive ( &lock );

		uint64_t value = pin < pins ? table [ pin ] : 0;

		ReleaseSRWLockExclusive ( &lock );

		return value;

	}

}