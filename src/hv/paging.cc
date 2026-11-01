#include "hv/paging.hh"

namespace hv {

	static constexpr uint64_t frame_mask = 0x000FFFFFFFFFF000ull;

	static uint64_t load_entry ( guest_bus& bus, uint64_t address, uint32_t size ) {

		uint8_t bytes [ 8 ] = { };
		uint64_t value = 0;

		bus.read ( address, bytes, size );

		for ( int32_t i = static_cast < int32_t > ( size ) - 1; i >= 0; i-- )
			value = value << 8 | bytes [ i ];

		return value;

	}

	static void mark_entry ( guest_bus& bus, uint64_t address, uint64_t entry, uint8_t bits ) {

		if ( ( entry & bits ) == bits )
			return;

		uint8_t low = static_cast < uint8_t > ( entry | bits );

		bus.write ( address, &low, 1 );

	}

	bool translate ( cpu_state& cpu, guest_bus& bus, uint64_t linear, bool write, uint64_t& physical ) {

		if ( !( cpu.cr0 & 0x80000000 ) ) {

			physical = linear & 0xFFFFFFFF;

			return true;

		}

		if ( !cpu.long_mode ( ) )
			linear &= 0xFFFFFFFF;

		bool user = ( cpu.segments [ SEG_CS ].selector & 3 ) == 3;
		bool protect = ( cpu.cr0 & 0x10000 ) != 0;
		bool writable = true;
		bool reachable = true;

		auto fault = [ & ] ( bool present ) {

			cpu.fault_address = linear;
			cpu.fault_code = ( present ? 1u : 0u ) | ( write ? 2u : 0u ) | ( user ? 4u : 0u );

			return false;

		};

		auto check = [ & ] ( uint64_t entry ) {

			writable = writable && ( entry & 2 );
			reachable = reachable && ( entry & 4 );

		};

		auto finish = [ & ] ( uint64_t frame, uint64_t offset_mask, uint64_t address, uint64_t entry ) {

			if ( ( user && !reachable ) || ( write && !writable && ( user || protect ) ) )
				return fault ( true );

			mark_entry ( bus, address, entry, write ? 0x60 : 0x20 );
			physical = frame | ( linear & offset_mask );

			return true;

		};

		if ( cpu.long_mode ( ) ) {

			int32_t levels = cpu.cr4 & 0x1000 ? 5 : 4;
			uint64_t table = cpu.cr3 & frame_mask;

			for ( int32_t level = levels; level >= 1; level-- ) {

				uint32_t shift = 12 + 9 * static_cast < uint32_t > ( level - 1 );
				uint64_t address = table + ( ( linear >> shift ) & 511 ) * 8;
				uint64_t entry = load_entry ( bus, address, 8 );

				if ( !( entry & 1 ) )
					return fault ( false );

				check ( entry );

				if ( level == 1 || ( ( level == 2 || level == 3 ) && ( entry & 0x80 ) ) ) {

					uint64_t size_mask = ( 1ull << shift ) - 1;

					return finish ( entry & frame_mask & ~size_mask, size_mask, address, entry );

				}

				mark_entry ( bus, address, entry, 0x20 );
				table = entry & frame_mask;

			}

			return fault ( false );

		}

		if ( cpu.cr4 & 0x20 ) {

			uint64_t directory = load_entry ( bus, ( cpu.cr3 & 0xFFFFFFE0 ) + ( ( linear >> 30 ) & 3 ) * 8, 8 );

			if ( !( directory & 1 ) )
				return fault ( false );

			uint64_t table = directory & frame_mask;

			for ( int32_t level = 2; level >= 1; level-- ) {

				uint32_t shift = 12 + 9 * static_cast < uint32_t > ( level - 1 );
				uint64_t address = table + ( ( linear >> shift ) & 511 ) * 8;
				uint64_t entry = load_entry ( bus, address, 8 );

				if ( !( entry & 1 ) )
					return fault ( false );

				check ( entry );

				if ( level == 1 || ( entry & 0x80 ) ) {

					uint64_t size_mask = ( 1ull << shift ) - 1;

					return finish ( entry & frame_mask & ~size_mask, size_mask, address, entry );

				}

				mark_entry ( bus, address, entry, 0x20 );
				table = entry & frame_mask;

			}

			return fault ( false );

		}

		uint64_t address = ( cpu.cr3 & 0xFFFFF000 ) + ( ( linear >> 22 ) & 1023 ) * 4;
		uint64_t entry = load_entry ( bus, address, 4 );

		if ( !( entry & 1 ) )
			return fault ( false );

		check ( entry );

		if ( ( entry & 0x80 ) && ( cpu.cr4 & 0x10 ) )
			return finish ( ( entry & 0xFFC00000 ) | ( ( entry >> 13 & 0xFF ) << 32 ), 0x3FFFFF, address, entry );

		mark_entry ( bus, address, entry, 0x20 );

		uint64_t page_address = ( entry & 0xFFFFF000 ) + ( ( linear >> 12 ) & 1023 ) * 4;
		uint64_t page = load_entry ( bus, page_address, 4 );

		if ( !( page & 1 ) )
			return fault ( false );

		check ( page );

		return finish ( page & 0xFFFFF000, 0xFFF, page_address, page );

	}

}