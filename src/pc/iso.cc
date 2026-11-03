#include "pc/iso.hh"
#include <windows.h>

namespace pc {

	static constexpr uint32_t sector = 2048;
	static const uint8_t stamp [ 7 ] = { 126, 11, 2, 0, 0, 0, 0 };

	static void report ( char* out, int32_t room, const char* text ) {

		int32_t used = 0;

		for ( ; out && text [ used ] && used < room - 1; used++ )
			out [ used ] = text [ used ];

		if ( out && room > 0 )
			out [ used ] = 0;

	}

	static void both32 ( uint8_t* out, uint32_t value ) {

		for ( int32_t i = 0; i < 4; i++ ) {

			out [ i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );
			out [ 7 - i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

		}

	}

	static void both16 ( uint8_t* out, uint16_t value ) {

		out [ 0 ] = static_cast < uint8_t > ( value );
		out [ 1 ] = static_cast < uint8_t > ( value >> 8 );
		out [ 2 ] = static_cast < uint8_t > ( value >> 8 );
		out [ 3 ] = static_cast < uint8_t > ( value );

	}

	static uint32_t sectors_for ( uint64_t bytes ) {

		return static_cast < uint32_t > ( ( bytes + sector - 1 ) / sector );

	}

	static char upper ( char c ) {

		return c >= 'a' && c <= 'z' ? static_cast < char > ( c - 32 ) : c;

	}

	static int32_t primary_name ( const char* name, uint8_t* out ) {

		int32_t length = 0;

		for ( ; name [ length ] && length < 64; length++ ) {

			char c = upper ( name [ length ] );
			bool plain = ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '.';

			out [ length ] = static_cast < uint8_t > ( plain ? c : '_' );

		}

		return length;

	}

	static int32_t joliet_name ( const char* name, uint8_t* out ) {

		int32_t length = 0;

		for ( ; name [ length ] && length < 64; length++ ) {

			out [ length * 2 ] = 0;
			out [ length * 2 + 1 ] = static_cast < uint8_t > ( name [ length ] );

		}

		return length * 2;

	}

	static int32_t compare ( const char* a, const char* b ) {

		for ( int32_t i = 0; ; i++ ) {

			char x = upper ( a [ i ] );
			char y = upper ( b [ i ] );

			if ( x != y || !x )
				return static_cast < int32_t > ( static_cast < uint8_t > ( x ) ) - static_cast < int32_t > ( static_cast < uint8_t > ( y ) );

		}

	}

	static void record ( base::list < uint8_t >& out, uint32_t extent, uint32_t size, bool folder, const uint8_t* name, int32_t name_length ) {

		int32_t length = 33 + name_length + ( name_length % 2 == 0 ? 1 : 0 );
		uint32_t used = static_cast < uint32_t > ( out.size ( ) % sector );

		if ( used + static_cast < uint32_t > ( length ) > sector ) {

			while ( out.size ( ) % sector )
				out.push ( 0 );

		}

		size_t start = out.size ( );

		out.resize ( start + static_cast < size_t > ( length ) );

		uint8_t* at = out.data ( ) + start;

		for ( int32_t i = 0; i < length; i++ )
			at [ i ] = 0;

		at [ 0 ] = static_cast < uint8_t > ( length );
		both32 ( at + 2, extent );
		both32 ( at + 10, size );

		for ( int32_t i = 0; i < 7; i++ )
			at [ 18 + i ] = stamp [ i ];

		at [ 25 ] = folder ? 2 : 0;
		both16 ( at + 28, 1 );
		at [ 32 ] = static_cast < uint8_t > ( name_length );

		for ( int32_t i = 0; i < name_length; i++ )
			at [ 33 + i ] = name [ i ];

	}

	void iso_builder::set_label ( const char* text ) {

		int32_t used = 0;

		for ( ; text [ used ] && used < 32; used++ )
			label [ used ] = upper ( text [ used ] );

		label [ used ] = 0;

	}

	void iso_builder::set_boot ( const char* path, uint16_t sectors ) {

		int32_t used = 0;

		for ( ; path [ used ] && used < 259; used++ )
			boot_path [ used ] = path [ used ];

		boot_path [ used ] = 0;
		boot_sectors = sectors ? sectors : 4;

	}

	int32_t iso_builder::find_child ( int32_t parent, const char* name, int32_t length ) const {

		for ( int32_t i = 0; i < nodes.count ( ); i++ ) {

			const node& item = nodes [ i ];

			if ( item.parent != parent || base::text_length ( item.name ) != length )
				continue;

			bool same = true;

			for ( int32_t c = 0; c < length && same; c++ )
				same = upper ( item.name [ c ] ) == upper ( name [ c ] );

			if ( same )
				return i;

		}

		return -1;

	}

	int32_t iso_builder::ensure_folder ( const char* path, int32_t length ) {

		if ( nodes.empty ( ) ) {

			node& root = nodes.emplace ( );

			root.name [ 0 ] = 0;
			root.parent = -1;
			root.folder = true;
			root.source [ 0 ] = 0;
			root.size = 0;

		}

		int32_t current = 0;
		int32_t start = 0;

		for ( int32_t i = 0; i <= length; i++ ) {

			if ( i < length && path [ i ] != '/' )
				continue;

			int32_t part = i - start;

			if ( part > 0 ) {

				int32_t found = find_child ( current, path + start, part );

				if ( found < 0 ) {

					node& item = nodes.emplace ( );

					for ( int32_t c = 0; c < part && c < 63; c++ )
						item.name [ c ] = path [ start + c ];

					item.name [ part < 63 ? part : 63 ] = 0;
					item.parent = current;
					item.folder = true;
					item.source [ 0 ] = 0;
					item.size = 0;
					found = nodes.count ( ) - 1;

				}

				current = found;

			}

			start = i + 1;

		}

		return current;

	}

	bool iso_builder::add_file ( const char* path, const wchar_t* source ) {

		WIN32_FILE_ATTRIBUTE_DATA data;

		if ( !GetFileAttributesExW ( source, GetFileExInfoStandard, &data ) || ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
			return false;

		int32_t length = base::text_length ( path );
		int32_t slash = -1;

		for ( int32_t i = 0; i < length; i++ ) {

			if ( path [ i ] == '/' )
				slash = i;

		}

		int32_t parent = ensure_folder ( path, slash < 0 ? 0 : slash );
		const char* name = path + slash + 1;

		if ( !name [ 0 ] || find_child ( parent, name, base::text_length ( name ) ) >= 0 )
			return false;

		node& item = nodes.emplace ( );
		int32_t used = 0;

		for ( ; name [ used ] && used < 63; used++ )
			item.name [ used ] = name [ used ];

		item.name [ used ] = 0;
		item.parent = parent;
		item.folder = false;
		item.size = static_cast < uint64_t > ( data.nFileSizeHigh ) << 32 | data.nFileSizeLow;

		for ( used = 0; source [ used ] && used < 259; used++ )
			item.source [ used ] = source [ used ];

		item.source [ used ] = 0;

		return true;

	}

	void iso_builder::children ( int32_t parent, base::list < int32_t >& out ) const {

		out.clear ( );

		for ( int32_t i = 0; i < nodes.count ( ); i++ ) {

			if ( nodes [ i ].parent == parent )
				out.push ( i );

		}

		const node* items = nodes.data ( );

		base::sort ( out.data ( ), out.count ( ), [ items ] ( int32_t a, int32_t b ) {

			return compare ( items [ a ].name, items [ b ].name ) < 0;

		} );

	}

	uint32_t iso_builder::directory_size ( int32_t folder, bool joliet ) const {

		base::list < uint8_t > bytes;

		directory ( folder, joliet, bytes );

		return sectors_for ( bytes.size ( ) ) * sector;

	}

	void iso_builder::directory ( int32_t folder, bool joliet, base::list < uint8_t >& out ) const {

		const node& self = nodes [ folder ];
		const node& parent = nodes [ self.parent < 0 ? 0 : self.parent ];
		uint8_t dot = 0;
		uint8_t dotdot = 1;
		base::list < int32_t > list;

		out.clear ( );
		record ( out, joliet ? self.joliet_extent : self.primary_extent, joliet ? self.joliet_size : self.primary_size, true, &dot, 1 );
		record ( out, joliet ? parent.joliet_extent : parent.primary_extent, joliet ? parent.joliet_size : parent.primary_size, true, &dotdot, 1 );
		children ( folder, list );

		for ( int32_t index : list ) {

			const node& item = nodes [ index ];
			uint8_t name [ 128 ];
			int32_t length = joliet ? joliet_name ( item.name, name ) : primary_name ( item.name, name );

			if ( item.folder )
				record ( out, joliet ? item.joliet_extent : item.primary_extent, joliet ? item.joliet_size : item.primary_size, true, name, length );
			else
				record ( out, item.extent, static_cast < uint32_t > ( item.size ), false, name, length );

		}

		while ( out.size ( ) % sector )
			out.push ( 0 );

	}

	void iso_builder::path_table ( bool joliet, bool big, base::list < uint8_t >& out ) const {

		base::list < int32_t > order;
		base::list < int32_t > list;

		out.clear ( );
		order.push ( 0 );

		for ( int32_t at = 0; at < order.count ( ); at++ ) {

			children ( order [ at ], list );

			for ( int32_t index : list ) {

				if ( nodes [ index ].folder )
					order.push ( index );

			}

		}

		for ( int32_t index : order ) {

			const node& item = nodes [ index ];
			uint8_t name [ 128 ] = { };
			int32_t length = index == 0 ? 1 : ( joliet ? joliet_name ( item.name, name ) : primary_name ( item.name, name ) );
			uint32_t extent = joliet ? item.joliet_extent : item.primary_extent;
			uint16_t parent = index == 0 ? 1 : nodes [ item.parent ].number;

			out.push ( static_cast < uint8_t > ( length ) );
			out.push ( 0 );

			for ( int32_t i = 0; i < 4; i++ )
				out.push ( static_cast < uint8_t > ( extent >> ( big ? ( 3 - i ) * 8 : i * 8 ) ) );

			out.push ( static_cast < uint8_t > ( big ? parent >> 8 : parent ) );
			out.push ( static_cast < uint8_t > ( big ? parent : parent >> 8 ) );

			for ( int32_t i = 0; i < length; i++ )
				out.push ( name [ i ] );

			if ( length % 2 )
				out.push ( 0 );

		}

	}

	void iso_builder::descriptor ( uint8_t type, bool joliet, uint32_t total, uint32_t table_size, uint32_t little, uint32_t big, uint8_t* out ) const {

		for ( uint32_t i = 0; i < sector; i++ )
			out [ i ] = 0;

		auto text = [ & ] ( uint32_t at, uint32_t room, const char* value ) {

			uint32_t length = static_cast < uint32_t > ( base::text_length ( value ) );

			if ( joliet ) {

				for ( uint32_t i = 0; i + 1 < room; i += 2 ) {

					out [ at + i ] = 0;
					out [ at + i + 1 ] = static_cast < uint8_t > ( i / 2 < length ? value [ i / 2 ] : ' ' );

				}

			} else {

				for ( uint32_t i = 0; i < room; i++ )
					out [ at + i ] = static_cast < uint8_t > ( i < length ? value [ i ] : ' ' );

			}

		};

		out [ 0 ] = type;
		out [ 1 ] = 'C';
		out [ 2 ] = 'D';
		out [ 3 ] = '0';
		out [ 4 ] = '0';
		out [ 5 ] = '1';
		out [ 6 ] = 1;
		text ( 8, 32, "" );
		text ( 40, 32, label );
		both32 ( out + 80, total );

		if ( joliet ) {

			out [ 88 ] = 0x25;
			out [ 89 ] = 0x2F;
			out [ 90 ] = 0x45;

		}

		both16 ( out + 120, 1 );
		both16 ( out + 124, 1 );
		both16 ( out + 128, static_cast < uint16_t > ( sector ) );
		both32 ( out + 132, table_size );

		for ( int32_t i = 0; i < 4; i++ ) {

			out [ 140 + i ] = static_cast < uint8_t > ( little >> ( i * 8 ) );
			out [ 148 + i ] = static_cast < uint8_t > ( big >> ( ( 3 - i ) * 8 ) );

		}

		const node& root = nodes [ 0 ];
		base::list < uint8_t > entry;
		uint8_t dot = 0;

		record ( entry, joliet ? root.joliet_extent : root.primary_extent, joliet ? root.joliet_size : root.primary_size, true, &dot, 1 );

		for ( int32_t i = 0; i < 34; i++ )
			out [ 156 + i ] = entry [ i ];

		text ( 190, 128, "" );
		text ( 318, 128, "" );
		text ( 446, 128, "" );
		text ( 574, 128, "COLDBOOT" );
		text ( 702, 37, "" );
		text ( 739, 37, "" );
		text ( 776, 37, "" );

		const char* created = "2026110200000000";
		const char* never = "0000000000000000";

		for ( int32_t i = 0; i < 16; i++ ) {

			out [ 813 + i ] = static_cast < uint8_t > ( created [ i ] );
			out [ 830 + i ] = static_cast < uint8_t > ( created [ i ] );
			out [ 847 + i ] = static_cast < uint8_t > ( never [ i ] );
			out [ 864 + i ] = static_cast < uint8_t > ( never [ i ] );

		}

		out [ 881 ] = 1;

	}

	bool iso_builder::write ( const wchar_t* target, char* error, int32_t room ) {

		if ( nodes.empty ( ) )
			ensure_folder ( "", 0 );

		int32_t boot = -1;

		if ( boot_path [ 0 ] ) {

			int32_t length = base::text_length ( boot_path );
			int32_t slash = -1;

			for ( int32_t i = 0; i < length; i++ ) {

				if ( boot_path [ i ] == '/' )
					slash = i;

			}

			int32_t parent = ensure_folder ( boot_path, slash < 0 ? 0 : slash );

			boot = find_child ( parent, boot_path + slash + 1, base::text_length ( boot_path + slash + 1 ) );

			if ( boot < 0 || nodes [ boot ].folder ) {

				report ( error, room, "The boot image isn't one of the files on the disc" );

				return false;

			}

		}

		base::list < int32_t > order;
		base::list < int32_t > list;

		order.push ( 0 );

		for ( int32_t at = 0; at < order.count ( ); at++ ) {

			nodes [ order [ at ] ].number = static_cast < uint16_t > ( at + 1 );
			children ( order [ at ], list );

			for ( int32_t index : list ) {

				if ( nodes [ index ].folder )
					order.push ( index );

			}

		}

		for ( int32_t index : order ) {

			nodes [ index ].primary_size = directory_size ( index, false );
			nodes [ index ].joliet_size = directory_size ( index, true );

		}

		uint32_t next = boot >= 0 ? 20 : 19;
		uint32_t catalog = 0;

		if ( boot >= 0 )
			catalog = next++;

		base::list < uint8_t > table;

		path_table ( false, false, table );

		uint32_t primary_table_size = static_cast < uint32_t > ( table.size ( ) );
		uint32_t primary_little = next;
		uint32_t primary_big = primary_little + sectors_for ( primary_table_size );

		next = primary_big + sectors_for ( primary_table_size );
		path_table ( true, false, table );

		uint32_t joliet_table_size = static_cast < uint32_t > ( table.size ( ) );
		uint32_t joliet_little = next;
		uint32_t joliet_big = joliet_little + sectors_for ( joliet_table_size );

		next = joliet_big + sectors_for ( joliet_table_size );

		for ( int32_t index : order ) {

			nodes [ index ].primary_extent = next;
			next += nodes [ index ].primary_size / sector;

		}

		for ( int32_t index : order ) {

			nodes [ index ].joliet_extent = next;
			next += nodes [ index ].joliet_size / sector;

		}

		for ( node& item : nodes ) {

			if ( item.folder )
				continue;

			item.extent = next;
			next += sectors_for ( item.size );

		}

		uint32_t total = next;
		HANDLE output = CreateFileW ( target, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( output == INVALID_HANDLE_VALUE ) {

			report ( error, room, "Couldn't create the ISO file (it may already exist)" );

			return false;

		}

		uint8_t* block = static_cast < uint8_t* > ( runtime_allocate_zero ( 0x100000 ) );
		bool ok = true;
		uint32_t written_sectors = 0;

		auto put = [ & ] ( const uint8_t* data, uint32_t size ) {

			DWORD done = 0;

			ok = ok && WriteFile ( output, data, size, &done, nullptr ) && done == size;
			written_sectors += size / sector;

		};

		auto pad_to = [ & ] ( uint32_t target_sector ) {

			for ( uint32_t i = 0; i < sector; i++ )
				block [ i ] = 0;

			while ( ok && written_sectors < target_sector )
				put ( block, sector );

		};

		auto put_list = [ & ] ( const base::list < uint8_t >& data ) {

			uint32_t size = static_cast < uint32_t > ( data.size ( ) );

			if ( size )
				put ( data.data ( ), size - size % sector );

			if ( size % sector ) {

				for ( uint32_t i = 0; i < sector; i++ )
					block [ i ] = i < size % sector ? data [ size - size % sector + i ] : 0;

				put ( block, sector );

			}

		};

		pad_to ( 16 );
		descriptor ( 1, false, total, primary_table_size, primary_little, primary_big, block );
		put ( block, sector );

		if ( boot >= 0 ) {

			for ( uint32_t i = 0; i < sector; i++ )
				block [ i ] = 0;

			const char* torito = "CD001\x01" "EL TORITO SPECIFICATION";

			for ( int32_t i = 0; i < 29; i++ )
				block [ 1 + i ] = static_cast < uint8_t > ( torito [ i ] );

			block [ 0 ] = 0;
			block [ 71 ] = static_cast < uint8_t > ( catalog );
			block [ 72 ] = static_cast < uint8_t > ( catalog >> 8 );
			block [ 73 ] = static_cast < uint8_t > ( catalog >> 16 );
			block [ 74 ] = static_cast < uint8_t > ( catalog >> 24 );
			put ( block, sector );

		}

		descriptor ( 2, true, total, joliet_table_size, joliet_little, joliet_big, block );
		put ( block, sector );

		for ( uint32_t i = 0; i < sector; i++ )
			block [ i ] = 0;

		block [ 0 ] = 255;
		block [ 1 ] = 'C';
		block [ 2 ] = 'D';
		block [ 3 ] = '0';
		block [ 4 ] = '0';
		block [ 5 ] = '1';
		block [ 6 ] = 1;
		put ( block, sector );

		if ( boot >= 0 ) {

			for ( uint32_t i = 0; i < sector; i++ )
				block [ i ] = 0;

			block [ 0 ] = 1;

			const char* name = "COLDBOOT";

			for ( int32_t i = 0; name [ i ]; i++ )
				block [ 4 + i ] = static_cast < uint8_t > ( name [ i ] );

			block [ 30 ] = 0x55;
			block [ 31 ] = 0xAA;

			uint32_t sum = 0;

			for ( int32_t i = 0; i < 32; i += 2 )
				sum += static_cast < uint32_t > ( block [ i ] ) | static_cast < uint32_t > ( block [ i + 1 ] ) << 8;

			uint16_t fix = static_cast < uint16_t > ( 0x10000 - ( sum & 0xFFFF ) );

			block [ 28 ] = static_cast < uint8_t > ( fix );
			block [ 29 ] = static_cast < uint8_t > ( fix >> 8 );
			block [ 32 ] = 0x88;
			block [ 33 ] = 0;
			block [ 38 ] = static_cast < uint8_t > ( boot_sectors );
			block [ 39 ] = static_cast < uint8_t > ( boot_sectors >> 8 );

			uint32_t start = nodes [ boot ].extent;

			for ( int32_t i = 0; i < 4; i++ )
				block [ 40 + i ] = static_cast < uint8_t > ( start >> ( i * 8 ) );

			put ( block, sector );

		}

		path_table ( false, false, table );
		put_list ( table );
		path_table ( false, true, table );
		put_list ( table );
		path_table ( true, false, table );
		put_list ( table );
		path_table ( true, true, table );
		put_list ( table );

		base::list < uint8_t > bytes;

		for ( int32_t index : order ) {

			directory ( index, false, bytes );
			put_list ( bytes );

		}

		for ( int32_t index : order ) {

			directory ( index, true, bytes );
			put_list ( bytes );

		}

		for ( node& item : nodes ) {

			if ( item.folder || !ok )
				continue;

			pad_to ( item.extent );

			HANDLE input = CreateFileW ( item.source, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr );

			if ( input == INVALID_HANDLE_VALUE ) {

				ok = false;

				break;

			}

			uint64_t left = item.size;

			while ( ok && left ) {

				uint32_t chunk = static_cast < uint32_t > ( base::min ( left, static_cast < uint64_t > ( 0x100000 ) ) );
				DWORD done = 0;

				if ( !ReadFile ( input, block, chunk, &done, nullptr ) || done != chunk ) {

					ok = false;

					break;

				}

				uint32_t padded = sectors_for ( chunk ) * sector;

				for ( uint32_t i = chunk; i < padded; i++ )
					block [ i ] = 0;

				put ( block, padded );
				left -= chunk;

			}

			CloseHandle ( input );

		}

		pad_to ( total );
		runtime_release ( block );
		CloseHandle ( output );

		if ( !ok ) {

			DeleteFileW ( target );
			report ( error, room, "Couldn't read one of the files or write the ISO" );

		}

		return ok;

	}

}