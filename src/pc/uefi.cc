#include "pc/uefi_parts.hh"
#include "hv/paging.hh"
#include "pc/acpi.hh"
#include "pc/pe.hh"
#include "pc/smbios.hh"

namespace pc {

	namespace efi {

		const uint8_t guid_loaded_image [ 16 ] = { 0xA1, 0x31, 0x1B, 0x5B, 0x62, 0x95, 0xD2, 0x11, 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B };
		const uint8_t guid_image_path [ 16 ] = { 0x7E, 0x15, 0x62, 0xBC, 0x33, 0x3E, 0xEC, 0x4F, 0x99, 0x20, 0x2D, 0x3B, 0x36, 0xD7, 0x50, 0xDF };
		const uint8_t guid_device_path [ 16 ] = { 0x91, 0x6E, 0x57, 0x09, 0x3F, 0x6D, 0xD2, 0x11, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B };
		const uint8_t guid_block_io [ 16 ] = { 0x21, 0x5B, 0x4E, 0x96, 0x59, 0x64, 0xD2, 0x11, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B };
		const uint8_t guid_disk_io [ 16 ] = { 0x71, 0x51, 0x34, 0xCE, 0x0B, 0xBA, 0xD2, 0x11, 0x8E, 0x4F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B };
		const uint8_t guid_text_in [ 16 ] = { 0xC1, 0x77, 0x74, 0x38, 0xC7, 0x69, 0xD2, 0x11, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B };
		const uint8_t guid_text_in_ex [ 16 ] = { 0x34, 0x75, 0x9E, 0xDD, 0x62, 0x77, 0x98, 0x46, 0x8C, 0x14, 0xF5, 0x85, 0x17, 0xA6, 0x25, 0xAA };
		const uint8_t guid_text_out [ 16 ] = { 0xC2, 0x77, 0x74, 0x38, 0xC7, 0x69, 0xD2, 0x11, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B };
		const uint8_t guid_graphics [ 16 ] = { 0xDE, 0xA9, 0x42, 0x90, 0xDC, 0x23, 0x38, 0x4A, 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A };
		const uint8_t guid_acpi_20 [ 16 ] = { 0x71, 0xE8, 0x68, 0x88, 0xF1, 0xE4, 0xD3, 0x11, 0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81 };
		const uint8_t guid_acpi_10 [ 16 ] = { 0x30, 0x2D, 0x9D, 0xEB, 0x88, 0x2D, 0xD3, 0x11, 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D };
		const uint8_t guid_smbios [ 16 ] = { 0x31, 0x2D, 0x9D, 0xEB, 0x88, 0x2D, 0xD3, 0x11, 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D };
		const uint8_t guid_global [ 16 ] = { 0x61, 0xDF, 0xE4, 0x8B, 0xCA, 0x93, 0xD2, 0x11, 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C };
		const uint8_t guid_exit_group [ 16 ] = { 0x55, 0xF0, 0xAB, 0x27, 0xB8, 0xB1, 0x26, 0x4C, 0x80, 0x48, 0x74, 0x8F, 0x37, 0xBA, 0xA2, 0xDF };
		const uint8_t guid_virtual_group [ 16 ] = { 0x98, 0x76, 0xFA, 0x13, 0x31, 0xC8, 0xC7, 0x49, 0x87, 0xEA, 0x8F, 0x43, 0xFC, 0xC2, 0x51, 0x96 };
		const uint8_t guid_system_partition [ 16 ] = { 0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B };

		bool same_guid ( const uint8_t* a, const uint8_t* b ) {

			for ( int32_t i = 0; i < 16; i++ ) {

				if ( a [ i ] != b [ i ] )
					return false;

			}

			return true;

		}

		uint32_t crc32 ( const uint8_t* data, uint64_t size, uint32_t seed ) {

			uint32_t crc = ~seed;

			for ( uint64_t i = 0; i < size; i++ ) {

				crc ^= data [ i ];

				for ( int32_t bit = 0; bit < 8; bit++ )
					crc = crc & 1 ? ( crc >> 1 ) ^ 0xEDB88320u : crc >> 1;

			}

			return ~crc;

		}

	}

	using namespace efi;

	static const uint8_t dispatch_code [ ] = {

		0xE7, 0xE4,
		0x4D, 0x85, 0xDB,
		0x75, 0x01,
		0xC3,
		0x49, 0x83, 0xFB, 0x02,
		0x74, 0xF2,
		0x48, 0x83, 0xEC, 0x28,
		0x41, 0xFF, 0xD2,
		0x48, 0x83, 0xC4, 0x28,
		0x48, 0x89, 0xC1,
		0xB8, 0xFF, 0xFF, 0xFF, 0xFF,
		0xEB, 0xDD,

	};

	uefi::uefi ( ) = default;

	uefi::~uefi ( ) = default;

	bool uefi::to_physical ( hv::cpu_state& cpu, uint64_t address, bool write, uint64_t& physical ) {

		if ( !( cpu.cr0 & 0x80000000u ) ) {

			physical = address;

			return true;

		}

		return hv::translate ( cpu, *vm, address, write, physical );

	}

	void uefi::get ( hv::cpu_state& cpu, uint64_t address, void* out, uint64_t size ) {

		uint8_t* bytes = static_cast < uint8_t* > ( out );

		while ( size ) {

			uint64_t chunk = page - ( address & ( page - 1 ) );
			uint64_t physical = 0;

			if ( chunk > size )
				chunk = size;

			if ( to_physical ( cpu, address, false, physical ) )
				vm->read ( physical, bytes, static_cast < uint32_t > ( chunk ) );
			else {

				for ( uint64_t i = 0; i < chunk; i++ )
					bytes [ i ] = 0;

			}

			address += chunk;
			bytes += chunk;
			size -= chunk;

		}

	}

	void uefi::put ( hv::cpu_state& cpu, uint64_t address, const void* data, uint64_t size ) {

		const uint8_t* bytes = static_cast < const uint8_t* > ( data );

		while ( size ) {

			uint64_t chunk = page - ( address & ( page - 1 ) );
			uint64_t physical = 0;

			if ( chunk > size )
				chunk = size;

			if ( to_physical ( cpu, address, true, physical ) )
				vm->write ( physical, bytes, static_cast < uint32_t > ( chunk ) );

			address += chunk;
			bytes += chunk;
			size -= chunk;

		}

	}

	uint64_t uefi::get64 ( hv::cpu_state& cpu, uint64_t address ) {

		uint64_t value = 0;

		get ( cpu, address, &value, 8 );

		return value;

	}

	uint32_t uefi::get32 ( hv::cpu_state& cpu, uint64_t address ) {

		uint32_t value = 0;

		get ( cpu, address, &value, 4 );

		return value;

	}

	void uefi::put64 ( hv::cpu_state& cpu, uint64_t address, uint64_t value ) {

		put ( cpu, address, &value, 8 );

	}

	void uefi::put32 ( hv::cpu_state& cpu, uint64_t address, uint32_t value ) {

		put ( cpu, address, &value, 4 );

	}

	void uefi::poke ( uint64_t address, const void* data, uint64_t size ) {

		const uint8_t* bytes = static_cast < const uint8_t* > ( data );

		while ( size ) {

			uint32_t chunk = static_cast < uint32_t > ( size > 0x100000 ? 0x100000 : size );

			vm->write ( address, bytes, chunk );
			address += chunk;
			bytes += chunk;
			size -= chunk;

		}

	}

	void uefi::poke64 ( uint64_t address, uint64_t value ) {

		poke ( address, &value, 8 );

	}

	void uefi::poke32 ( uint64_t address, uint32_t value ) {

		poke ( address, &value, 4 );

	}

	void uefi::poke16 ( uint64_t address, uint16_t value ) {

		poke ( address, &value, 2 );

	}

	uint64_t uefi::argument ( hv::cpu_state& cpu, uint32_t index ) {

		static const uint32_t registers [ 4 ] = { 1, 2, 8, 9 };

		if ( index < 4 )
			return cpu.gpr [ registers [ index ] ];

		return get64 ( cpu, cpu.gpr [ hv::REG_SP ] + 0x28 + ( index - 4 ) * 8ull );

	}

	uint64_t uefi::heap ( uint64_t size ) {

		uint64_t at = ( heap_next + 15 ) & ~15ull;

		if ( at + size > heap_end )
			return 0;

		heap_next = at + size;

		return at;

	}

	uint64_t uefi::stub ( uint32_t id ) {

		uint32_t slot = id >= EXCEPTION_BASE ? 256 + ( id - EXCEPTION_BASE ) : id;
		uint64_t address = stubs + 64 + slot * 16ull;
		int32_t jump = static_cast < int32_t > ( static_cast < int64_t > ( stubs ) - static_cast < int64_t > ( address + 10 ) );
		uint8_t bytes [ 10 ] = { 0xB8, static_cast < uint8_t > ( id ), static_cast < uint8_t > ( id >> 8 ), static_cast < uint8_t > ( id >> 16 ), static_cast < uint8_t > ( id >> 24 ), 0xE9, static_cast < uint8_t > ( jump ), static_cast < uint8_t > ( jump >> 8 ), static_cast < uint8_t > ( jump >> 16 ), static_cast < uint8_t > ( jump >> 24 ) };

		poke ( address, bytes, 10 );

		return address;

	}

	void uefi::mark ( uint64_t start, uint64_t pages, uint32_t type ) {

		uint64_t end = start + pages * page;
		base::list < region > kept;

		for ( const region& item : regions ) {

			uint64_t item_end = item.start + item.pages * page;

			if ( item_end <= start || item.start >= end ) {

				kept.push ( item );

				continue;

			}

			if ( item.start < start )
				kept.push ( { item.start, ( start - item.start ) / page, item.type } );

			if ( item_end > end )
				kept.push ( { end, ( item_end - end ) / page, item.type } );

		}

		kept.push ( { start, pages, type } );

		for ( int32_t i = 1; i < kept.count ( ); i++ ) {

			for ( int32_t j = i; j > 0 && kept [ j - 1 ].start > kept [ j ].start; j-- ) {

				region swap = kept [ j ];

				kept [ j ] = kept [ j - 1 ];
				kept [ j - 1 ] = swap;

			}

		}

		regions.clear ( );

		for ( const region& item : kept ) {

			if ( !item.pages )
				continue;

			if ( !regions.empty ( ) && regions.back ( ).type == item.type && regions.back ( ).start + regions.back ( ).pages * page == item.start )
				regions.back ( ).pages += item.pages;
			else
				regions.push ( item );

		}

		map_key++;

	}

	uint64_t uefi::allocate ( uint32_t type, uint64_t pages, uint64_t below, uint64_t exact ) {

		if ( !pages )
			pages = 1;

		uint64_t bytes = pages * page;

		if ( exact != ~0ull ) {

			if ( exact & ( page - 1 ) )
				return 0;

			for ( const region& item : regions ) {

				if ( item.type == MEMORY_FREE && exact >= item.start && exact + bytes <= item.start + item.pages * page ) {

					mark ( exact, pages, type );

					return exact;

				}

			}

			return 0;

		}

		for ( int32_t pass = 0; pass < 2; pass++ ) {

			uint64_t ceiling = pass == 0 ? ( below < 0x100000000ull ? below : 0x100000000ull ) : below;

			for ( int32_t i = regions.count ( ) - 1; i >= 0; i-- ) {

				const region& item = regions [ i ];

				if ( item.type != MEMORY_FREE )
					continue;

				uint64_t top = item.start + item.pages * page;

				if ( top > ceiling )
					top = ceiling & ~( page - 1 );

				if ( top < item.start + bytes )
					continue;

				uint64_t start = top - bytes;

				mark ( start, pages, type );

				return start;

			}

		}

		return 0;

	}

	bool uefi::release_pages ( uint64_t start, uint64_t pages ) {

		uint64_t end = start + pages * page;

		for ( const region& item : regions ) {

			if ( item.start <= start && end <= item.start + item.pages * page ) {

				if ( item.type == MEMORY_FREE || item.type == MEMORY_RUNTIME_CODE || item.type == MEMORY_RUNTIME_DATA || item.type == MEMORY_ACPI_NVS || item.type == MEMORY_RESERVED )
					return false;

				mark ( start, pages, MEMORY_FREE );

				return true;

			}

		}

		return false;

	}

	uint64_t uefi::pool ( uint32_t type, uint64_t size ) {

		size = ( size + 15 ) & ~15ull;

		if ( !size )
			size = 16;

		if ( size > 0x10000 ) {

			uint64_t pages = ( size + page - 1 ) / page;
			uint64_t address = allocate ( type, pages, ~0ull, ~0ull );

			if ( !address )
				return 0;

			for ( auto& slot : big_pools ) {

				if ( !slot [ 0 ] ) {

					slot [ 0 ] = address;
					slot [ 1 ] = pages;
					break;

				}

			}

			return address;

		}

		for ( arena& item : arenas ) {

			if ( item.type == type && item.next + size <= item.end ) {

				uint64_t address = item.next;

				item.next += size;

				return address;

			}

		}

		uint64_t pages = 64;
		uint64_t address = allocate ( type, pages, ~0ull, ~0ull );

		if ( !address )
			return 0;

		static const uint8_t zeros [ 4096 ] = { };

		for ( uint64_t i = 0; i < pages; i++ )
			poke ( address + i * page, zeros, page );

		arenas.push ( { type, address + size, address + pages * page } );

		return address;

	}

	uint64_t uefi::new_handle ( ) {

		uint64_t handle = heap ( 16 );

		poke32 ( handle, 0x4C444E48 );

		return handle;

	}

	void uefi::install_protocol ( uint64_t handle, const uint8_t* guid, uint64_t interface ) {

		for ( installed& item : protocols ) {

			if ( item.handle == handle && same_guid ( item.guid, guid ) ) {

				item.interface = interface;

				return;

			}

		}

		installed item;

		item.handle = handle;
		item.interface = interface;

		for ( int32_t i = 0; i < 16; i++ )
			item.guid [ i ] = guid [ i ];

		protocols.push ( item );

	}

	uint64_t uefi::find_protocol ( uint64_t handle, const uint8_t* guid ) const {

		for ( const installed& item : protocols ) {

			if ( item.handle == handle && same_guid ( item.guid, guid ) )
				return item.interface;

		}

		return 0;

	}

	bool uefi::known_handle ( uint64_t handle ) const {

		for ( const installed& item : protocols ) {

			if ( item.handle == handle )
				return true;

		}

		return false;

	}

	void uefi::log ( const char* text ) {

		for ( int32_t i = 0; text [ i ]; i++ )
			link.bus->console ( text [ i ] );

	}

	void uefi::build_memory ( ) {

		uint64_t low = vm->core.low_size;
		uint64_t high = vm->core.high_size ( );

		firmware_size = 0x1000000;
		firmware = ( low - firmware_size ) & ~0xFFFFFull;
		regions.clear ( );
		arenas.clear ( );
		regions.push ( { 0, 1, MEMORY_BOOT_DATA } );
		regions.push ( { 0x1000, ( 0xA0000 - 0x1000 ) / page, MEMORY_FREE } );
		regions.push ( { 0x100000, ( low - 0x100000 ) / page, MEMORY_FREE } );

		if ( high )
			regions.push ( { 0x100000000ull, high / page, MEMORY_FREE } );

		mark ( firmware, 0x100000 / page, MEMORY_BOOT_DATA );
		mark ( firmware + 0x100000, 0x10000 / page, MEMORY_RUNTIME_CODE );
		mark ( firmware + 0x110000, 0x10000 / page, MEMORY_RUNTIME_DATA );
		mark ( firmware + 0x120000, 0x400000 / page, MEMORY_BOOT_DATA );
		mark ( low - 0x80000, 0x40000 / page, MEMORY_RESERVED );
		mark ( low - 0x40000, 0x40000 / page, MEMORY_ACPI_NVS );

		uint8_t* zeros = static_cast < uint8_t* > ( runtime_allocate_zero ( 0x100000 ) );

		for ( uint64_t at = 0; at < 0x520000; at += 0x100000 )
			poke ( firmware + at, zeros, base::min ( 0x100000ull, 0x520000ull - at ) );

		runtime_release ( zeros );
		heap_next = firmware + 0x120000;
		heap_end = heap_next + 0x400000;
		stubs = firmware + 0x100000;
		stack_top = firmware + 0x100000;

		uint8_t* tables = static_cast < uint8_t* > ( runtime_allocate_zero ( 0x42000 ) );
		uint64_t* entries = reinterpret_cast < uint64_t* > ( tables );

		entries [ 0 ] = ( firmware + 0x1000 ) | 3;

		for ( uint64_t i = 0; i < 64; i++ ) {

			entries [ 0x1000 / 8 + i ] = ( firmware + 0x2000 + i * page ) | 3;

			for ( uint64_t j = 0; j < 512; j++ )
				entries [ ( 0x2000 + i * page ) / 8 + j ] = ( i << 30 | j << 21 ) | 0x83;

		}

		poke ( firmware, tables, 0x42000 );
		runtime_release ( tables );

		static const uint64_t descriptors [ 5 ] = { 0, 0x00AF9A000000FFFFull, 0x00CF92000000FFFFull, 0x00CF9A000000FFFFull, 0x00CF92000000FFFFull };

		poke ( firmware + 0x50000, descriptors, sizeof ( descriptors ) );
		poke ( stubs, dispatch_code, sizeof ( dispatch_code ) );

		for ( uint32_t v = 0; v < 256; v++ ) {

			uint64_t handler = stub ( EXCEPTION_BASE + v );
			uint64_t gate [ 2 ] = { ( handler & 0xFFFF ) | 0x08ull << 16 | 0x8E00ull << 32 | ( handler >> 16 & 0xFFFF ) << 48, handler >> 32 };

			poke ( firmware + 0x51000 + v * 16ull, gate, 16 );

		}

	}

	void uefi::build_tables ( ) {

		uint64_t low = vm->core.low_size;
		acpi_layout layout;
		acpi_image* acpi = new acpi_image ( );

		layout.processors = link.processors;
		layout.low_memory = low;
		layout.high_memory = vm->core.high_size ( );
		layout.tables = static_cast < uint32_t > ( low - 0x40000 );
		build_acpi ( layout, *acpi );
		poke ( layout.tables, acpi->tables.data ( ), acpi->tables.size ( ) );
		rsdp = layout.tables + ( ( acpi->tables.size ( ) + 15 ) & ~15ull );
		poke ( rsdp, acpi->rsdp, 36 );
		delete acpi;

		smbios_layout shape;
		smbios_image* smbios = new smbios_image ( );

		shape.processors = link.processors;
		shape.low_memory = low;
		shape.high_memory = vm->core.high_size ( );
		shape.seed = link.seed;
		shape.table_address = static_cast < uint32_t > ( low - 0x80000 + 0x100 );
		build_smbios ( shape, *smbios );
		smbios_entry = low - 0x80000;
		poke ( smbios_entry, smbios->entry, 31 );
		poke ( shape.table_address, smbios->table.data ( ), smbios->table.size ( ) );
		delete smbios;

		uint64_t data = firmware + 0x110000;

		system_table = data;
		runtime_table = data + 0x100;
		config_table = data + 0x200;
		vendor = data + 0x400;
		boot_table = heap ( 376 );

		static const char name [ ] = "Coldboot";

		for ( int32_t i = 0; name [ i ]; i++ )
			poke16 ( vendor + i * 2ull, static_cast < uint16_t > ( name [ i ] ) );

		poke64 ( boot_table, 0x56524553544F4F42ull );
		poke32 ( boot_table + 8, 0x00020046 );
		poke32 ( boot_table + 12, 376 );

		for ( uint32_t i = 0; i < BOOT_SERVICE_COUNT; i++ )
			poke64 ( boot_table + 24 + i * 8ull, stub ( i ) );

		poke64 ( runtime_table, 0x56524553544E5552ull );
		poke32 ( runtime_table + 8, 0x00020046 );
		poke32 ( runtime_table + 12, 136 );

		for ( uint32_t i = RT_GET_TIME; i < RUNTIME_SERVICE_END; i++ )
			poke64 ( runtime_table + 24 + ( i - RT_GET_TIME ) * 8ull, stub ( i ) );

		poke64 ( system_table, 0x5453595320494249ull );
		poke32 ( system_table + 8, 0x00020046 );
		poke32 ( system_table + 12, 120 );
		poke64 ( system_table + 24, vendor );
		poke32 ( system_table + 32, 0x00010000 );
		poke64 ( system_table + 88, runtime_table );
		poke64 ( system_table + 96, boot_table );

		const uint8_t* guids [ 3 ] = { guid_acpi_20, guid_acpi_10, guid_smbios };
		uint64_t places [ 3 ] = { rsdp, rsdp, smbios_entry };

		config_count = 3;

		for ( uint32_t i = 0; i < 3; i++ ) {

			poke ( config_table + i * 24ull, guids [ i ], 16 );
			poke64 ( config_table + i * 24ull + 16, places [ i ] );

		}

		poke64 ( system_table + 104, config_count );
		poke64 ( system_table + 112, config_table );

	}

	void uefi::signal ( event& item ) {

		if ( item.closed )
			return;

		if ( ( item.type & 0x200 ) && item.function ) {

			pending.push ( { item.function, item.handle, item.context } );
			item.signaled = false;

			return;

		}

		item.signaled = true;

	}

	void uefi::check_timers ( ) {

		uint64_t now = GetTickCount64 ( ) * 10000;

		for ( event& item : events ) {

			if ( item.closed || !item.due || now < item.due )
				continue;

			item.due = item.period ? now + item.period : 0;
			signal ( item );

		}

	}

	uefi::event* uefi::find_event ( uint64_t handle ) {

		for ( event& item : events ) {

			if ( item.handle == handle && !item.closed )
				return &item;

		}

		return nullptr;

	}

	void uefi::finish ( hv::cpu_state& cpu, uint64_t status ) {

		check_timers ( );

		if ( !pending.empty ( ) && tpl < 31 ) {

			frame item;

			item.kind = FRAME_RETURN;
			item.status = status;
			frames.push ( item );

			callback next = pending [ 0 ];

			pending.remove_at ( 0 );
			start_callback ( cpu, next.function, next.first, next.second );

			return;

		}

		cpu.gpr [ hv::REG_AX ] = status;
		cpu.gpr [ 11 ] = 0;

	}

	void uefi::start_callback ( hv::cpu_state& cpu, uint64_t function, uint64_t first, uint64_t second ) {

		cpu.gpr [ 10 ] = function;
		cpu.gpr [ hv::REG_CX ] = first;
		cpu.gpr [ hv::REG_DX ] = second;
		cpu.gpr [ 8 ] = 0;
		cpu.gpr [ 9 ] = 0;
		cpu.gpr [ 11 ] = 1;

	}

	void uefi::retry ( hv::cpu_state& cpu, uint32_t id ) {

		cpu.gpr [ hv::REG_AX ] = id;
		cpu.gpr [ 11 ] = 2;

	}

	void uefi::continue_frame ( hv::cpu_state& cpu ) {

		if ( frames.empty ( ) ) {

			cpu.gpr [ hv::REG_AX ] = 0;
			cpu.gpr [ 11 ] = 0;

			return;

		}

		frame top = frames.back ( );

		if ( top.kind == FRAME_RETURN || top.kind == FRAME_RETRY ) {

			if ( !pending.empty ( ) ) {

				callback next = pending [ 0 ];

				pending.remove_at ( 0 );
				start_callback ( cpu, next.function, next.first, next.second );

				return;

			}

			frames.pop ( );

			if ( top.kind == FRAME_RETRY ) {

				cpu.gpr [ hv::REG_CX ] = top.saved.gpr [ hv::REG_CX ];
				cpu.gpr [ hv::REG_DX ] = top.saved.gpr [ hv::REG_DX ];
				cpu.gpr [ 8 ] = top.saved.gpr [ 8 ];
				cpu.gpr [ 9 ] = top.saved.gpr [ 9 ];
				retry ( cpu, static_cast < uint32_t > ( top.status ) );

				return;

			}

			cpu.gpr [ hv::REG_AX ] = top.status;
			cpu.gpr [ 11 ] = 0;

			return;

		}

		frames.pop ( );

		if ( top.kind == FRAME_IMAGE ) {

			finish ( cpu, cpu.gpr [ hv::REG_CX ] );

			return;

		}

		char text [ 96 ] = "boot image returned ";
		static const char digits [ ] = "0123456789ABCDEF";
		uint64_t status = cpu.gpr [ hv::REG_CX ];
		int32_t used = 20;

		for ( int32_t shift = 60; shift >= 0; shift -= 4 )
			text [ used++ ] = digits [ status >> shift & 15 ];

		text [ used++ ] = '\n';
		text [ used ] = 0;
		log ( text );
		firmware_main ( cpu );

	}

	bool uefi::load_image ( const uint8_t* file, uint32_t size, uint64_t parent, uint64_t device, const uint8_t* path, uint32_t path_size, uint64_t& handle, char* why ) {

		pe_info info;

		if ( !pe_inspect ( file, size, info ) || info.machine != 0x8664 ) {

			copy_text ( why, "not an x64 EFI program", 64 );

			return false;

		}

		uint32_t code_type = info.subsystem == 11 ? MEMORY_BOOT_CODE : ( info.subsystem == 12 ? MEMORY_RUNTIME_CODE : MEMORY_LOADER_CODE );
		uint64_t pages = ( info.image_size + page - 1 ) / page;
		uint64_t base = allocate ( code_type, pages, ~0ull, ~0ull );

		if ( !base ) {

			copy_text ( why, "out of memory for the program", 64 );

			return false;

		}

		uint8_t* placed = static_cast < uint8_t* > ( runtime_allocate_zero ( info.image_size ) );
		bool ok = pe_place ( file, size, placed, base );

		if ( ok )
			poke ( base, placed, info.image_size );

		runtime_release ( placed );

		if ( !ok ) {

			release_pages ( base, pages );
			copy_text ( why, "the program's sections didn't fit", 64 );

			return false;

		}

		handle = new_handle ( );

		uint64_t loaded = heap ( 96 );
		uint64_t file_path = 0;
		uint64_t full_path = 0;

		if ( path_size ) {

			file_path = heap ( path_size );
			poke ( file_path, path, path_size );

			const block_device* owner = nullptr;

			for ( const block_device& item : disks ) {

				if ( item.handle == device )
					owner = &item;

			}

			if ( owner ) {

				full_path = heap ( owner->path_size - 4 + path_size );

				uint8_t* joined = static_cast < uint8_t* > ( runtime_allocate ( owner->path_size + path_size ) );

				vm->read ( owner->path, joined, owner->path_size - 4 );

				for ( uint32_t i = 0; i < path_size; i++ )
					joined [ owner->path_size - 4 + i ] = path [ i ];

				poke ( full_path, joined, owner->path_size - 4 + path_size );
				runtime_release ( joined );

			} else
				full_path = file_path;

		}

		poke32 ( loaded, 0x1000 );
		poke64 ( loaded + 8, parent );
		poke64 ( loaded + 16, system_table );
		poke64 ( loaded + 24, device );
		poke64 ( loaded + 32, file_path );
		poke64 ( loaded + 64, base );
		poke64 ( loaded + 72, info.image_size );
		poke32 ( loaded + 80, code_type );
		poke32 ( loaded + 84, code_type + 1 );
		install_protocol ( handle, guid_loaded_image, loaded );

		if ( full_path )
			install_protocol ( handle, guid_image_path, full_path );

		image item;

		item.handle = handle;
		item.base = base;
		item.pages = pages;
		item.entry = base + info.entry;
		item.loaded = loaded;
		images.push ( item );

		return true;

	}

	void uefi::firmware_main ( hv::cpu_state& cpu ) {

		char why [ 128 ] = { };

		if ( next_boot ( why ) ) {

			frame item;

			item.kind = FRAME_MAIN;
			item.image = images.back ( ).handle;
			item.saved = cpu;
			frames.push ( item );
			start_callback ( cpu, images.back ( ).entry, images.back ( ).handle, system_table );

			return;

		}

		if ( boot_stage < 100 ) {

			static const char message [ ] = "No bootable device. Insert an EFI disc or use a disk with an EFI system partition.\r\nPress any key to try the boot devices again.\r\n";
			uint16_t wide [ sizeof ( message ) ];

			for ( uint32_t i = 0; i < sizeof ( message ); i++ )
				wide [ i ] = static_cast < uint8_t > ( message [ i ] );

			text_out ( wide, sizeof ( message ) - 1 );
			boot_stage = 100;
			key_count = 0;
			copy_text ( last_boot, "nothing bootable", sizeof ( last_boot ) );

		}

		uint16_t scan = 0;
		uint16_t unicode = 0;

		if ( poll_key ( scan, unicode ) ) {

			key_count = 0;
			boot_stage = 0;
			clear_text ( );
			firmware_main ( cpu );

			return;

		}

		Sleep ( 50 );
		retry ( cpu, FIRMWARE_MAIN );

	}

	void uefi::call ( uint32_t processor, uint32_t value, hv::cpu_state& cpu ) {

		InterlockedIncrement ( &calls );
		AcquireSRWLockExclusive ( &lock );
		cpu.gpr [ 11 ] = 0;

		if ( value == RESUME )
			continue_frame ( cpu );
		else if ( value >= EXCEPTION_BASE && value < EXCEPTION_BASE + 256 ) {

			uint32_t vector = value - EXCEPTION_BASE;
			bool coded = vector == 8 || ( vector >= 10 && vector <= 14 ) || vector == 17 || vector == 21 || vector == 29 || vector == 30;
			uint64_t rip = get64 ( cpu, cpu.gpr [ hv::REG_SP ] + ( coded ? 8 : 0 ) );
			static const char digits [ ] = "0123456789ABCDEF";

			if ( !trouble [ 0 ] ) {

				char text [ 160 ] = "firmware caught exception ";
				int32_t used = 26;

				text [ used++ ] = digits [ vector >> 4 & 15 ];
				text [ used++ ] = digits [ vector & 15 ];

				for ( const char* part = " at "; *part; part++ )
					text [ used++ ] = *part;

				for ( int32_t shift = 60; shift >= 0; shift -= 4 )
					text [ used++ ] = digits [ rip >> shift & 15 ];

				text [ used++ ] = '\n';
				text [ used ] = 0;
				copy_text ( trouble, text, sizeof ( trouble ) );
				log ( text );

			}

			ReleaseSRWLockExclusive ( &lock );
			Sleep ( 20 );
			retry ( cpu, value );

			return;

		} else if ( value == FIRMWARE_MAIN )
			firmware_main ( cpu );
		else {

			bool handled = true;
			uint64_t status = service ( value, cpu, handled );

			if ( handled && ( status & fault ) && value < 100 && value != BS_CHECK_EVENT ) {

				static const char digits [ ] = "0123456789ABCDEF";
				char text [ 32 ] = "call ";
				int32_t used = 5;

				text [ used++ ] = digits [ value / 10 % 10 ];
				text [ used++ ] = digits [ value % 10 ];
				text [ used++ ] = ' ';
				text [ used++ ] = digits [ status >> 4 & 15 ];
				text [ used++ ] = digits [ status & 15 ];
				text [ used++ ] = '\n';
				text [ used ] = 0;
				log ( text );

			}

			if ( handled )
				finish ( cpu, status );

		}

		ReleaseSRWLockExclusive ( &lock );

		if ( processor && cpu.gpr [ 11 ] == 2 )
			Sleep ( 1 );

	}

	uint64_t uefi::service ( uint32_t id, hv::cpu_state& cpu, bool& handled ) {

		if ( id < BOOT_SERVICE_COUNT ) {

			if ( exited && id != BS_COPY_MEMORY && id != BS_SET_MEMORY && id != BS_CRC32 )
				return UNSUPPORTED;

			return boot_service ( id, cpu, handled );

		}

		if ( id == RT_RESET ) {

			if ( argument ( cpu, 0 ) == 2 )
				link.bus->request_power_off ( );
			else
				link.bus->request_reset ( );

			handled = false;
			Sleep ( 5 );
			retry ( cpu, id );

			return 0;

		}

		if ( id >= RT_GET_TIME && id < RUNTIME_SERVICE_END )
			return runtime_service ( id, cpu );

		if ( exited )
			return UNSUPPORTED;

		return device_service ( id, cpu, handled );

	}

	bool uefi::install ( const uefi_links& links, char* error, int32_t room ) {

		link = links;
		vm = links.vm;
		protocols.clear ( );
		events.clear ( );
		images.clear ( );
		disks.clear ( );
		frames.clear ( );
		pending.clear ( );
		variables.clear ( );
		mappings.clear ( );
		exited = false;
		virtual_mode = false;
		boot_stage = 0;
		key_count = 0;
		answer_prompt = false;
		tpl = 4;
		last_boot [ 0 ] = 0;
		trouble [ 0 ] = 0;

		for ( auto& slot : big_pools ) {

			slot [ 0 ] = 0;
			slot [ 1 ] = 0;

		}

		if ( vm->core.low_size < 0x4000000 ) {

			copy_text ( error, "UEFI needs at least 64 MB of memory", room );

			return false;

		}

		build_memory ( );
		build_tables ( );
		build_console ( );
		build_graphics ( );
		build_disks ( );
		build_variables ( );

		uint8_t header [ 120 ];

		vm->read ( system_table, header, 120 );

		for ( int32_t i = 16; i < 20; i++ )
			header [ i ] = 0;

		poke32 ( system_table + 16, crc32 ( header, 120, 0 ) );

		uint8_t services [ 376 ];

		vm->read ( boot_table, services, 376 );

		for ( int32_t i = 16; i < 20; i++ )
			services [ i ] = 0;

		poke32 ( boot_table + 16, crc32 ( services, 376, 0 ) );
		vm->read ( runtime_table, services, 136 );

		for ( int32_t i = 16; i < 20; i++ )
			services [ i ] = 0;

		poke32 ( runtime_table + 16, crc32 ( services, 136, 0 ) );

		dual_pic& pics = *link.pics;

		pics.write ( 0x20, 1, 0x11 );
		pics.write ( 0x21, 1, 0x20 );
		pics.write ( 0x21, 1, 0x04 );
		pics.write ( 0x21, 1, 0x01 );
		pics.write ( 0x21, 1, 0xFF );
		pics.write ( 0xA0, 1, 0x11 );
		pics.write ( 0xA1, 1, 0x28 );
		pics.write ( 0xA1, 1, 0x02 );
		pics.write ( 0xA1, 1, 0x01 );
		pics.write ( 0xA1, 1, 0xFF );
		vm->set_mtrr ( 0x2FF, 0xC06 );
		vm->set_mtrr ( 0x250, 0x0606060606060606ull );
		vm->set_mtrr ( 0x258, 0x0606060606060606ull );
		vm->set_mtrr ( 0x259, 0 );

		for ( uint32_t msr = 0x268; msr <= 0x26F; msr++ )
			vm->set_mtrr ( msr, 0x0505050505050505ull );

		vm->set_mtrr ( 0x200, 0xC0000000ull );
		vm->set_mtrr ( 0x201, 0xFFFFC0000800ull );

		hv::cpu_state cpu;

		vm->get_state ( 0, cpu );

		for ( uint64_t& value : cpu.gpr )
			value = 0;

		for ( hv::segment& item : cpu.segments )
			item = { 0, 0xFFFFFFFF, 0x10, 0xC093 };

		cpu.segments [ hv::SEG_CS ] = { 0, 0xFFFFFFFF, 0x08, 0xA09B };
		cpu.gpr [ hv::REG_SP ] = stack_top - 8;
		cpu.rip = stub ( FIRMWARE_MAIN );
		cpu.rflags = 2;
		cpu.cr0 = 0x80010033;
		cpu.cr3 = firmware;
		cpu.cr4 = 0x620;
		cpu.efer = 0x500;
		poke64 ( stack_top - 8, 0 );

		if ( !vm->set_state ( 0, cpu ) || !vm->set_tables ( 0, firmware + 0x50000, 0x27, firmware + 0x51000, 0xFFF ) ) {

			copy_text ( error, "Couldn't put the first processor into 64-bit mode", room );

			return false;

		}

		vm->attach_call ( port, this );

		return true;

	}

}