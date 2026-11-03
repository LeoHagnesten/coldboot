#include "pc/uefi_parts.hh"
#include "base/files.hh"
#include "video/cga.hh"

namespace pc {

	using namespace efi;

	struct graphics_choice {

		uint32_t width;
		uint32_t height;
		uint16_t number;

	};

	static const graphics_choice graphics_modes [ ] = {

		{ 1024, 768, 0x118 },
		{ 800, 600, 0x115 },
		{ 640, 480, 0x112 },
		{ 1280, 720, 0x140 },
		{ 1280, 1024, 0x11B },
		{ 1600, 900, 0x144 },
		{ 1920, 1080, 0x147 },

	};

	static constexpr uint32_t graphics_count = sizeof ( graphics_modes ) / sizeof ( graphics_modes [ 0 ] );

	static const uint32_t text_palette [ 16 ] = {

		0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
		0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,

	};

	static const char keys_plain [ 0x3A ] = {

		0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9,
		'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13, 0, 'a', 's',
		'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
		'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',

	};

	static const char keys_shifted [ 0x3A ] = {

		0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8, 9,
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 13, 0, 'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
		'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',

	};

	static uint8_t glyph_for ( uint16_t character ) {

		if ( character < 0x80 )
			return static_cast < uint8_t > ( character );

		static const uint16_t wide [ ] = { 0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x2550, 0x2551, 0x2554, 0x2557, 0x255A, 0x255D, 0x2588, 0x2591, 0x25B2, 0x25BC, 0x25BA, 0x25C4, 0x2191, 0x2193 };
		static const uint8_t narrow [ ] = { 0xC4, 0xB3, 0xDA, 0xBF, 0xC0, 0xD9, 0xCD, 0xBA, 0xC9, 0xBB, 0xC8, 0xBC, 0xDB, 0xB0, 0x1E, 0x1F, 0x10, 0x11, 0x18, 0x19 };

		for ( uint32_t i = 0; i < sizeof ( wide ) / sizeof ( wide [ 0 ] ); i++ ) {

			if ( wide [ i ] == character )
				return narrow [ i ];

		}

		return '?';

	}

	static void path_root ( base::list < uint8_t >& out ) {

		static const uint8_t root [ ] = { 0x02, 0x01, 0x0C, 0x00, 0xD0, 0x41, 0x03, 0x0A, 0x00, 0x00, 0x00, 0x00 };

		for ( uint8_t value : root )
			out.push ( value );

	}

	static void path_node ( base::list < uint8_t >& out, const uint8_t* node, uint32_t size ) {

		for ( uint32_t i = 0; i < size; i++ )
			out.push ( node [ i ] );

	}

	static void path_end ( base::list < uint8_t >& out ) {

		static const uint8_t end [ 4 ] = { 0x7F, 0xFF, 0x04, 0x00 };

		path_node ( out, end, 4 );

	}

	static void put_u32 ( uint8_t* at, uint32_t value ) {

		for ( int32_t i = 0; i < 4; i++ )
			at [ i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

	}

	static void put_u64 ( uint8_t* at, uint64_t value ) {

		put_u32 ( at, static_cast < uint32_t > ( value ) );
		put_u32 ( at + 4, static_cast < uint32_t > ( value >> 32 ) );

	}

	static uint32_t le32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24;

	}

	static uint64_t le64 ( const uint8_t* at ) {

		return static_cast < uint64_t > ( le32 ( at ) ) | static_cast < uint64_t > ( le32 ( at + 4 ) ) << 32;

	}

	void uefi::build_console ( ) {

		console_handle = new_handle ( );

		event key;

		key.handle = heap ( 16 );
		key.type = 0x100;
		key.keyboard = true;
		events.push ( key );
		key_event = key.handle;
		text_in = heap ( 24 );
		poke64 ( text_in, stub ( IN_RESET ) );
		poke64 ( text_in + 8, stub ( IN_READ_KEY ) );
		poke64 ( text_in + 16, key_event );
		text_in_ex = heap ( 48 );
		poke64 ( text_in_ex, stub ( IN_EX_RESET ) );
		poke64 ( text_in_ex + 8, stub ( IN_EX_READ_KEY ) );
		poke64 ( text_in_ex + 16, key_event );
		poke64 ( text_in_ex + 24, stub ( IN_EX_SET_STATE ) );
		poke64 ( text_in_ex + 32, stub ( IN_EX_REGISTER ) );
		poke64 ( text_in_ex + 40, stub ( IN_EX_UNREGISTER ) );
		text_mode = heap ( 24 );
		text_out_interface = heap ( 80 );

		for ( uint32_t i = 0; i < 9; i++ )
			poke64 ( text_out_interface + i * 8ull, stub ( OUT_RESET + i ) );

		poke64 ( text_out_interface + 72, text_mode );

		base::list < uint8_t > path;
		static const uint8_t display [ 6 ] = { 0x01, 0x01, 0x06, 0x00, 0x00, 0x02 };

		path_root ( path );
		path_node ( path, display, 6 );
		path_end ( path );

		uint64_t path_at = heap ( path.size ( ) );

		poke ( path_at, path.data ( ), path.size ( ) );
		install_protocol ( console_handle, guid_text_in, text_in );
		install_protocol ( console_handle, guid_text_in_ex, text_in_ex );
		install_protocol ( console_handle, guid_text_out, text_out_interface );
		install_protocol ( console_handle, guid_device_path, path_at );
		poke64 ( system_table + 40, console_handle );
		poke64 ( system_table + 48, text_in );
		poke64 ( system_table + 56, console_handle );
		poke64 ( system_table + 64, text_out_interface );
		poke64 ( system_table + 72, console_handle );
		poke64 ( system_table + 80, text_out_interface );
		text_attribute = 0x07;
		text_mode_number = 0;
		cursor_column = 0;
		cursor_row = 0;

	}

	void uefi::build_graphics ( ) {

		graphics_info = heap ( 36 );
		graphics_mode = heap ( 40 );
		graphics = heap ( 32 );
		poke64 ( graphics, stub ( GOP_QUERY ) );
		poke64 ( graphics + 8, stub ( GOP_SET ) );
		poke64 ( graphics + 16, stub ( GOP_BLT ) );
		poke64 ( graphics + 24, graphics_mode );
		poke32 ( graphics_mode, graphics_count );
		poke64 ( graphics_mode + 8, graphics_info );
		poke64 ( graphics_mode + 16, 36 );
		install_protocol ( console_handle, guid_graphics, graphics );
		set_graphics ( 0 );

	}

	bool uefi::set_graphics ( uint32_t mode ) {

		if ( mode >= graphics_count )
			return false;

		vga& video = *link.video;

		vm->lock_devices ( );

		bool ok = video.set_vbe ( graphics_modes [ mode ].number );

		vm->unlock_devices ( );

		if ( !ok )
			return false;

		graphics_number = mode;

		uint32_t info [ 9 ] = { 0, video.vbe_width, video.vbe_height, 1, 0, 0, 0, 0, video.vbe_pitch / 4 };

		poke ( graphics_info, info, 36 );
		poke32 ( graphics_mode + 4, mode );
		poke64 ( graphics_mode + 24, video.lfb_address ( ) );
		poke64 ( graphics_mode + 32, static_cast < uint64_t > ( video.vbe_pitch ) * video.vbe_height );
		text_width = video.vbe_width / 8;
		text_height = video.vbe_height / 16;

		if ( text_mode_number == 2 ) {

			text_columns = text_width;
			text_rows = text_height;

		}

		clear_text ( );

		return true;

	}

	void uefi::sync_text_mode ( ) {

		int32_t values [ 5 ] = { 3, text_mode_number, static_cast < int32_t > ( text_attribute ), cursor_column, cursor_row };

		poke ( text_mode, values, 20 );

		uint8_t visible = 1;

		poke ( text_mode + 20, &visible, 1 );

	}

	void uefi::draw_cell ( int32_t column, int32_t row, uint16_t character ) {

		vga& video = *link.video;
		int32_t x = column * 8;
		int32_t y = row * 16;

		if ( x + 8 > video.vbe_width || y + 16 > video.vbe_height || video.vbe_bits != 32 )
			return;

		const uint8_t* shape = machine::font_8x8 [ glyph_for ( character ) ];
		uint32_t foreground = text_palette [ text_attribute & 15 ];
		uint32_t background = text_palette [ text_attribute >> 4 & 7 ];

		for ( int32_t line = 0; line < 16; line++ ) {

			uint32_t* pixels = reinterpret_cast < uint32_t* > ( video.vram + static_cast < uint64_t > ( y + line ) * video.vbe_pitch ) + x;
			uint8_t bits = shape [ line / 2 ];

			for ( int32_t dot = 0; dot < 8; dot++ )
				pixels [ dot ] = bits & ( 0x80 >> dot ) ? foreground : background;

		}

	}

	void uefi::clear_text ( ) {

		vga& video = *link.video;
		uint32_t background = text_palette [ text_attribute >> 4 & 7 ];

		if ( video.vbe_bits == 32 ) {

			for ( uint32_t y = 0; y < video.vbe_height; y++ ) {

				uint32_t* pixels = reinterpret_cast < uint32_t* > ( video.vram + static_cast < uint64_t > ( y ) * video.vbe_pitch );

				for ( uint32_t x = 0; x < video.vbe_width; x++ )
					pixels [ x ] = background;

			}

		}

		cursor_column = 0;
		cursor_row = 0;
		sync_text_mode ( );

	}

	void uefi::scroll_text ( ) {

		vga& video = *link.video;
		uint32_t pitch = video.vbe_pitch;
		uint32_t used = static_cast < uint32_t > ( text_rows ) * 16;
		uint32_t background = text_palette [ text_attribute >> 4 & 7 ];

		if ( video.vbe_bits != 32 || used > video.vbe_height )
			return;

		for ( uint32_t y = 0; y + 16 < used; y++ ) {

			uint8_t* target = video.vram + static_cast < uint64_t > ( y ) * pitch;
			const uint8_t* source = target + 16ull * pitch;

			for ( uint32_t i = 0; i < pitch; i++ )
				target [ i ] = source [ i ];

		}

		for ( uint32_t y = used - 16; y < used; y++ ) {

			uint32_t* pixels = reinterpret_cast < uint32_t* > ( video.vram + static_cast < uint64_t > ( y ) * pitch );

			for ( uint32_t x = 0; x < video.vbe_width; x++ )
				pixels [ x ] = background;

		}

	}

	void uefi::text_out ( const uint16_t* text, uint32_t count ) {

		for ( uint32_t i = 0; i < count; i++ ) {

			uint16_t character = text [ i ];

			if ( character < 0x80 && character != 0 )
				link.bus->console ( static_cast < char > ( character ) );

			if ( character == '\r' )
				cursor_column = 0;
			else if ( character == '\n' )
				cursor_row++;
			else if ( character == 8 ) {

				if ( cursor_column > 0 )
					cursor_column--;

			} else if ( character ) {

				if ( cursor_column >= text_columns ) {

					cursor_column = 0;
					cursor_row++;

				}

				if ( cursor_row >= text_rows ) {

					scroll_text ( );
					cursor_row = text_rows - 1;

				}

				draw_cell ( cursor_column, cursor_row, character );
				cursor_column++;

			}

			if ( cursor_row >= text_rows ) {

				scroll_text ( );
				cursor_row = text_rows - 1;

			}

		}

		sync_text_mode ( );

	}

	void uefi::pump_keyboard ( ) {

		keyboard_controller& keyboard = *link.keyboard;

		vm->lock_devices ( );

		for ( int32_t guard = 0; guard < 64; guard++ ) {

			uint8_t status = static_cast < uint8_t > ( keyboard.read ( 0x64, 1 ) );

			if ( !( status & 1 ) )
				break;

			uint8_t data = static_cast < uint8_t > ( keyboard.read ( 0x60, 1 ) );

			if ( status & 0x20 )
				continue;

			if ( data == 0xE0 ) {

				extended = true;

				continue;

			}

			bool released = ( data & 0x80 ) != 0;
			uint8_t code = data & 0x7F;

			if ( code == 0x2A || code == 0x36 )
				shift = !released;
			else if ( code == 0x1D )
				control = !released;
			else if ( code == 0x38 )
				alt = !released;
			else if ( !released ) {

				uint16_t scan = 0;
				uint16_t unicode = 0;

				switch ( code ) {

					case 0x48: scan = 1; break;
					case 0x50: scan = 2; break;
					case 0x4D: scan = 3; break;
					case 0x4B: scan = 4; break;
					case 0x47: scan = 5; break;
					case 0x4F: scan = 6; break;
					case 0x52: scan = 7; break;
					case 0x53: scan = 8; break;
					case 0x49: scan = 9; break;
					case 0x51: scan = 10; break;
					case 0x57: scan = 0x15; break;
					case 0x58: scan = 0x16; break;
					case 0x01: scan = 0x17; break;

					default:

						if ( code >= 0x3B && code <= 0x44 )
							scan = static_cast < uint16_t > ( 0x0B + code - 0x3B );
						else if ( code < 0x3A && !extended )
							unicode = static_cast < uint16_t > ( static_cast < uint8_t > ( shift ? keys_shifted [ code ] : keys_plain [ code ] ) );
						else if ( code == 0x1C )
							unicode = 13;
						else if ( code == 0x35 )
							unicode = '/';

						break;

				}

				if ( unicode == 27 )
					unicode = 0;

				if ( ( scan || unicode ) && key_count < 32 ) {

					keys [ key_count ] [ 0 ] = scan;
					keys [ key_count ] [ 1 ] = unicode;
					key_count++;

				}

			}

			extended = false;

		}

		vm->unlock_devices ( );

	}

	bool uefi::poll_key ( uint16_t& scan, uint16_t& unicode ) {

		pump_keyboard ( );

		if ( !key_count && answer_prompt ) {

			answer_prompt = false;
			scan = 0;
			unicode = ' ';

			return true;

		}

		if ( !key_count )
			return false;

		scan = keys [ 0 ] [ 0 ];
		unicode = keys [ 0 ] [ 1 ];

		for ( uint32_t i = 1; i < key_count; i++ ) {

			keys [ i - 1 ] [ 0 ] = keys [ i ] [ 0 ];
			keys [ i - 1 ] [ 1 ] = keys [ i ] [ 1 ];

		}

		key_count--;

		return true;

	}

	void uefi::add_block_device ( disk_file* media, uint64_t offset, uint64_t blocks, uint32_t block_size, bool read_only, bool partition, const uint8_t* path, uint32_t path_size, bool system ) {

		block_device item;
		uint8_t description [ 48 ] = { };

		item.handle = new_handle ( );
		item.media = media;
		item.offset = offset;
		item.blocks = blocks;
		item.block_size = block_size;
		item.read_only = read_only;
		item.partition = partition;
		item.system = system;
		put_u32 ( description, media == link.optical ? 1 : 0 );
		description [ 4 ] = media == link.optical ? 1 : 0;
		description [ 5 ] = 1;
		description [ 6 ] = partition ? 1 : 0;
		description [ 7 ] = read_only ? 1 : 0;
		put_u32 ( description + 12, block_size );
		put_u64 ( description + 24, blocks ? blocks - 1 : 0 );
		put_u32 ( description + 40, 1 );

		uint64_t media_at = heap ( 48 );

		poke ( media_at, description, 48 );
		item.block_io = heap ( 48 );
		poke64 ( item.block_io, 0x00020001 );
		poke64 ( item.block_io + 8, media_at );
		poke64 ( item.block_io + 16, stub ( BLOCK_RESET ) );
		poke64 ( item.block_io + 24, stub ( BLOCK_READ ) );
		poke64 ( item.block_io + 32, stub ( BLOCK_WRITE ) );
		poke64 ( item.block_io + 40, stub ( BLOCK_FLUSH ) );
		item.disk_io = heap ( 24 );
		poke64 ( item.disk_io, 0x00010000 );
		poke64 ( item.disk_io + 8, stub ( DISK_READ ) );
		poke64 ( item.disk_io + 16, stub ( DISK_WRITE ) );
		item.path = heap ( path_size );
		item.path_size = path_size;
		poke ( item.path, path, path_size );
		install_protocol ( item.handle, guid_device_path, item.path );
		install_protocol ( item.handle, guid_block_io, item.block_io );
		install_protocol ( item.handle, guid_disk_io, item.disk_io );
		disks.push ( item );

	}

	void uefi::build_disks ( ) {

		static const uint8_t controller [ 6 ] = { 0x01, 0x01, 0x06, 0x00, 0x01, 0x01 };

		if ( link.hard_disk && link.hard_disk->present ( ) ) {

			disk_file* disk = link.hard_disk;
			base::list < uint8_t > path;
			static const uint8_t channel [ 8 ] = { 0x03, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 };

			path_root ( path );
			path_node ( path, controller, 6 );
			path_node ( path, channel, 8 );

			uint32_t parent_size = static_cast < uint32_t > ( path.size ( ) );

			path_end ( path );
			add_block_device ( disk, 0, disk->size / 512, 512, !disk->writable, false, path.data ( ), static_cast < uint32_t > ( path.size ( ) ), false );

			uint8_t header [ 512 ];
			uint8_t master [ 512 ];

			disk->read ( 0, master, 512 );

			if ( disk->read ( 512, header, 512 ) && le64 ( header ) == 0x5452415020494645ull ) {

				uint64_t table = le64 ( header + 72 );
				uint32_t count = le32 ( header + 80 );
				uint32_t size = le32 ( header + 84 );

				for ( uint32_t i = 0; i < count && i < 128 && size >= 128 && size <= 512; i++ ) {

					uint8_t entry [ 128 ];

					if ( !disk->read ( table * 512 + static_cast < uint64_t > ( i ) * size, entry, 128 ) )
						break;

					bool used = false;

					for ( int32_t b = 0; b < 16; b++ )
						used = used || entry [ b ];

					if ( !used )
						continue;

					uint64_t first = le64 ( entry + 32 );
					uint64_t last = le64 ( entry + 40 );

					if ( last < first || last >= disk->size / 512 )
						continue;

					uint8_t node [ 42 ] = { 0x04, 0x01, 42, 0x00 };

					put_u32 ( node + 4, i + 1 );
					put_u64 ( node + 8, first );
					put_u64 ( node + 16, last - first + 1 );

					for ( int32_t b = 0; b < 16; b++ )
						node [ 24 + b ] = entry [ 16 + b ];

					node [ 40 ] = 2;
					node [ 41 ] = 2;
					path.resize ( parent_size );
					path_node ( path, node, 42 );
					path_end ( path );
					add_block_device ( disk, first * 512, last - first + 1, 512, !disk->writable, true, path.data ( ), static_cast < uint32_t > ( path.size ( ) ), same_guid ( entry, guid_system_partition ) );

				}

			} else if ( master [ 510 ] == 0x55 && master [ 511 ] == 0xAA ) {

				for ( uint32_t i = 0; i < 4; i++ ) {

					const uint8_t* entry = master + 446 + i * 16;
					uint8_t type = entry [ 4 ];
					uint32_t first = le32 ( entry + 8 );
					uint32_t length = le32 ( entry + 12 );

					if ( !type || type == 0xEE || type == 0x05 || type == 0x0F || !length )
						continue;

					uint8_t node [ 42 ] = { 0x04, 0x01, 42, 0x00 };

					put_u32 ( node + 4, i + 1 );
					put_u64 ( node + 8, first );
					put_u64 ( node + 16, length );

					for ( int32_t b = 0; b < 4; b++ )
						node [ 24 + b ] = master [ 440 + b ];

					node [ 40 ] = 1;
					node [ 41 ] = 1;
					path.resize ( parent_size );
					path_node ( path, node, 42 );
					path_end ( path );
					add_block_device ( disk, static_cast < uint64_t > ( first ) * 512, length, 512, !disk->writable, true, path.data ( ), static_cast < uint32_t > ( path.size ( ) ), type == 0xEF );

				}

			}

		}

		if ( link.optical && link.optical->present ( ) ) {

			disk_file* disc = link.optical;
			base::list < uint8_t > path;
			static const uint8_t channel [ 8 ] = { 0x03, 0x01, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00 };

			path_root ( path );
			path_node ( path, controller, 6 );
			path_node ( path, channel, 8 );

			uint32_t parent_size = static_cast < uint32_t > ( path.size ( ) );
			uint64_t blocks = disc->size / 2048;

			path_end ( path );
			add_block_device ( disc, 0, blocks, 2048, true, false, path.data ( ), static_cast < uint32_t > ( path.size ( ) ), false );

			uint8_t record [ 2048 ];
			static const char marker [ ] = "EL TORITO SPECIFICATION";
			bool torito = disc->read ( 17 * 2048ull, record, 2048 ) && record [ 0 ] == 0 && record [ 1 ] == 'C' && record [ 2 ] == 'D';

			for ( int32_t i = 0; torito && marker [ i ]; i++ )
				torito = record [ 7 + i ] == static_cast < uint8_t > ( marker [ i ] );

			uint8_t catalog [ 2048 ];

			if ( torito && disc->read ( static_cast < uint64_t > ( le32 ( record + 0x47 ) ) * 2048, catalog, 2048 ) && catalog [ 0 ] == 1 && catalog [ 30 ] == 0x55 && catalog [ 31 ] == 0xAA ) {

				uint8_t platform = catalog [ 1 ];
				uint32_t index = 0;
				uint32_t left = 1;

				for ( uint32_t at = 32; at + 32 <= 2048; at += 32, index++ ) {

					const uint8_t* entry = catalog + at;

					if ( at > 32 && !left ) {

						if ( entry [ 0 ] != 0x90 && entry [ 0 ] != 0x91 )
							break;

						platform = entry [ 1 ];
						left = static_cast < uint32_t > ( entry [ 2 ] | entry [ 3 ] << 8 );

						continue;

					}

					if ( left )
						left--;

					if ( entry [ 0 ] != 0x88 || platform != 0xEF )
						continue;

					uint32_t rba = le32 ( entry + 8 );
					uint64_t bytes = static_cast < uint64_t > ( entry [ 6 ] | entry [ 7 ] << 8 ) * 512;
					uint8_t boot [ 512 ];

					if ( disc->read ( static_cast < uint64_t > ( rba ) * 2048, boot, 512 ) && boot [ 510 ] == 0x55 && boot [ 511 ] == 0xAA ) {

						uint32_t sector = static_cast < uint32_t > ( boot [ 11 ] | boot [ 12 ] << 8 );
						uint32_t total = static_cast < uint32_t > ( boot [ 19 ] | boot [ 20 ] << 8 );

						if ( !total )
							total = le32 ( boot + 32 );

						if ( static_cast < uint64_t > ( total ) * sector > bytes )
							bytes = static_cast < uint64_t > ( total ) * sector;

					}

					uint64_t length = ( bytes + 2047 ) / 2048;

					if ( !length || rba + length > blocks )
						continue;

					uint8_t node [ 24 ] = { 0x04, 0x02, 24, 0x00 };

					put_u32 ( node + 4, index );
					put_u64 ( node + 8, rba );
					put_u64 ( node + 16, length );
					path.resize ( parent_size );
					path_node ( path, node, 24 );
					path_end ( path );
					add_block_device ( disc, static_cast < uint64_t > ( rba ) * 2048, length, 2048, true, true, path.data ( ), static_cast < uint32_t > ( path.size ( ) ), true );

				}

			}

		}

	}

	bool uefi::next_boot ( char* why ) {

		static const char* const names [ 2 ] = { "\\EFI\\BOOT\\BOOTX64.EFI", "\\EFI\\Microsoft\\Boot\\bootmgfw.efi" };

		while ( boot_stage < 2 ) {

			int32_t stage = boot_stage++;
			bool disc_turn = ( stage == 0 ) == link.optical_first;
			disk_file* wanted = disc_turn ? link.optical : link.hard_disk;

			if ( !wanted || !wanted->present ( ) )
				continue;

			for ( const block_device& device : disks ) {

				if ( device.media != wanted || !device.system )
					continue;

				for ( const char* name : names ) {

					base::list < uint8_t > file;

					if ( !read_file ( device, name, file ) )
						continue;

					base::list < uint8_t > node;
					uint32_t length = 0;

					while ( name [ length ] )
						length++;

					uint32_t size = 4 + ( length + 1 ) * 2;

					node.push ( 0x04 );
					node.push ( 0x04 );
					node.push ( static_cast < uint8_t > ( size ) );
					node.push ( static_cast < uint8_t > ( size >> 8 ) );

					for ( uint32_t i = 0; i <= length; i++ ) {

						node.push ( static_cast < uint8_t > ( name [ i ] ) );
						node.push ( 0 );

					}

					path_end ( node );

					uint64_t handle = 0;

					if ( !load_image ( file.data ( ), static_cast < uint32_t > ( file.size ( ) ), 0, device.handle, node.data ( ), static_cast < uint32_t > ( node.size ( ) ), handle, why ) )
						continue;

					copy_text ( last_boot, disc_turn ? "UEFI dvd" : "UEFI hard disk", sizeof ( last_boot ) );
					log ( disc_turn ? "UEFI: booting the dvd\n" : "UEFI: booting the hard disk\n" );

					return true;

				}

			}

		}

		return false;

	}

	uefi::block_device* uefi::find_block ( uint64_t interface, bool disk_io ) {

		for ( block_device& item : disks ) {

			if ( ( disk_io ? item.disk_io : item.block_io ) == interface )
				return &item;

		}

		return nullptr;

	}

	uint64_t uefi::block_transfer ( hv::cpu_state& cpu, block_device& device, uint64_t byte_offset, uint64_t size, uint64_t buffer, bool writing ) {

		uint64_t room = size < 0x100000 ? size : 0x100000;
		uint8_t* chunk = static_cast < uint8_t* > ( runtime_allocate ( room ) );
		uint64_t status = SUCCESS;

		for ( uint64_t done = 0; done < size; ) {

			uint32_t take = static_cast < uint32_t > ( size - done < room ? size - done : room );

			if ( writing ) {

				get ( cpu, buffer + done, chunk, take );

				if ( !device.media->write ( device.offset + byte_offset + done, chunk, take ) ) {

					status = DEVICE_ERROR;
					break;

				}

			} else {

				if ( !device.media->read ( device.offset + byte_offset + done, chunk, take ) ) {

					status = DEVICE_ERROR;
					break;

				}

				put ( cpu, buffer + done, chunk, take );

			}

			done += take;

		}

		runtime_release ( chunk );

		return status;

	}

	uint64_t uefi::blt ( hv::cpu_state& cpu ) {

		vga& video = *link.video;
		uint64_t buffer = argument ( cpu, 1 );
		uint64_t operation = argument ( cpu, 2 );
		uint64_t source_x = argument ( cpu, 3 );
		uint64_t source_y = argument ( cpu, 4 );
		uint64_t target_x = argument ( cpu, 5 );
		uint64_t target_y = argument ( cpu, 6 );
		uint64_t width = argument ( cpu, 7 );
		uint64_t height = argument ( cpu, 8 );
		uint64_t delta = argument ( cpu, 9 );
		uint64_t screen_width = video.vbe_width;
		uint64_t screen_height = video.vbe_height;
		uint64_t pitch = video.vbe_pitch;

		if ( !width || !height )
			return INVALID_PARAMETER;

		if ( !delta )
			delta = width * 4;

		if ( operation == 0 || operation == 2 || operation == 3 ) {

			if ( target_x + width > screen_width || target_y + height > screen_height )
				return INVALID_PARAMETER;

		}

		if ( operation == 1 || operation == 3 ) {

			if ( source_x + width > screen_width || source_y + height > screen_height )
				return INVALID_PARAMETER;

		}

		if ( operation == 0 ) {

			uint32_t color = get32 ( cpu, buffer );

			for ( uint64_t y = 0; y < height; y++ ) {

				uint32_t* row = reinterpret_cast < uint32_t* > ( video.vram + ( target_y + y ) * pitch ) + target_x;

				for ( uint64_t x = 0; x < width; x++ )
					row [ x ] = color;

			}

			return SUCCESS;

		}

		if ( operation == 1 ) {

			for ( uint64_t y = 0; y < height; y++ )
				put ( cpu, buffer + ( target_y + y ) * delta + target_x * 4, video.vram + ( source_y + y ) * pitch + source_x * 4, width * 4 );

			return SUCCESS;

		}

		if ( operation == 2 ) {

			for ( uint64_t y = 0; y < height; y++ )
				get ( cpu, buffer + ( source_y + y ) * delta + source_x * 4, video.vram + ( target_y + y ) * pitch + target_x * 4, width * 4 );

			return SUCCESS;

		}

		if ( operation == 3 ) {

			bool upward = target_y > source_y;

			for ( uint64_t step = 0; step < height; step++ ) {

				uint64_t y = upward ? height - 1 - step : step;
				uint8_t* target = video.vram + ( target_y + y ) * pitch + target_x * 4;
				const uint8_t* source = video.vram + ( source_y + y ) * pitch + source_x * 4;

				if ( target > source ) {

					for ( uint64_t i = width * 4; i > 0; i-- )
						target [ i - 1 ] = source [ i - 1 ];

				} else {

					for ( uint64_t i = 0; i < width * 4; i++ )
						target [ i ] = source [ i ];

				}

			}

			return SUCCESS;

		}

		return INVALID_PARAMETER;

	}

	uint64_t uefi::device_service ( uint32_t id, hv::cpu_state& cpu, bool& handled ) {

		switch ( id ) {

			case IN_RESET:
			case IN_EX_RESET:
				pump_keyboard ( );
				key_count = 0;
				return SUCCESS;

			case IN_READ_KEY:
			case IN_EX_READ_KEY: {

				uint64_t out = argument ( cpu, 1 );
				uint16_t scan = 0;
				uint16_t unicode = 0;

				if ( !out )
					return INVALID_PARAMETER;

				if ( !poll_key ( scan, unicode ) )
					return NOT_READY;

				uint8_t data [ 12 ] = { static_cast < uint8_t > ( scan ), static_cast < uint8_t > ( scan >> 8 ), static_cast < uint8_t > ( unicode ), static_cast < uint8_t > ( unicode >> 8 ) };

				if ( id == IN_EX_READ_KEY ) {

					put_u32 ( data + 4, 0x80000000u | ( shift ? 0x01u : 0u ) | ( control ? 0x04u : 0u ) | ( alt ? 0x10u : 0u ) );
					data [ 8 ] = 0x80;

				}

				put ( cpu, out, data, id == IN_EX_READ_KEY ? 9 : 4 );

				return SUCCESS;

			}

			case IN_EX_SET_STATE:
			case IN_EX_UNREGISTER:
				return SUCCESS;

			case IN_EX_REGISTER:

				if ( argument ( cpu, 3 ) )
					put64 ( cpu, argument ( cpu, 3 ), heap ( 16 ) );

				return SUCCESS;

			case OUT_RESET:
			case OUT_CLEAR:
				clear_text ( );
				return SUCCESS;

			case OUT_STRING: {

				uint64_t text = argument ( cpu, 1 );
				uint16_t buffer [ 512 ];

				for ( uint32_t total = 0; text && total < 8192; total += 512 ) {

					get ( cpu, text + total * 2ull, buffer, sizeof ( buffer ) );

					uint32_t count = 0;

					while ( count < 512 && buffer [ count ] )
						count++;

					text_out ( buffer, count );

					static const char prompt [ ] = "boot from CD or DVD";

					for ( uint32_t start = 0; start + sizeof ( prompt ) - 1 <= count && !answer_prompt; start++ ) {

						uint32_t matched = 0;

						while ( prompt [ matched ] && buffer [ start + matched ] == static_cast < uint16_t > ( prompt [ matched ] ) )
							matched++;

						if ( prompt [ matched ] )
							continue;

						bool installed = false;

						for ( const block_device& device : disks )
							installed = installed || ( device.system && device.media == link.hard_disk );

						answer_prompt = !installed;

					}

					if ( count < 512 )
						break;

				}

				return SUCCESS;

			}

			case OUT_TEST:
				return SUCCESS;

			case OUT_QUERY_MODE: {

				uint64_t mode = argument ( cpu, 1 );
				uint64_t columns_at = argument ( cpu, 2 );
				uint64_t rows_at = argument ( cpu, 3 );

				if ( mode == 1 || mode > 2 || ( mode == 2 && ( text_width < 80 || text_height < 25 ) ) )
					return UNSUPPORTED;

				if ( columns_at )
					put64 ( cpu, columns_at, mode == 0 ? 80 : static_cast < uint64_t > ( text_width ) );

				if ( rows_at )
					put64 ( cpu, rows_at, mode == 0 ? 25 : static_cast < uint64_t > ( text_height ) );

				return SUCCESS;

			}

			case OUT_SET_MODE: {

				uint64_t mode = argument ( cpu, 1 );

				if ( mode == 1 || mode > 2 || ( mode == 2 && ( text_width < 80 || text_height < 25 ) ) )
					return UNSUPPORTED;

				text_mode_number = static_cast < int32_t > ( mode );
				text_columns = mode == 0 ? 80 : text_width;
				text_rows = mode == 0 ? 25 : text_height;
				clear_text ( );

				return SUCCESS;

			}

			case OUT_ATTRIBUTE:
				text_attribute = static_cast < uint32_t > ( argument ( cpu, 1 ) & 0x7F );
				sync_text_mode ( );
				return SUCCESS;

			case OUT_CURSOR: {

				uint64_t column = argument ( cpu, 1 );
				uint64_t row = argument ( cpu, 2 );

				if ( column >= static_cast < uint64_t > ( text_columns ) || row >= static_cast < uint64_t > ( text_rows ) )
					return UNSUPPORTED;

				cursor_column = static_cast < int32_t > ( column );
				cursor_row = static_cast < int32_t > ( row );
				sync_text_mode ( );

				return SUCCESS;

			}

			case OUT_ENABLE_CURSOR:
				return SUCCESS;

			case GOP_QUERY: {

				uint64_t mode = argument ( cpu, 1 );
				uint64_t size_at = argument ( cpu, 2 );
				uint64_t info_at = argument ( cpu, 3 );

				if ( mode >= graphics_count || !size_at || !info_at )
					return INVALID_PARAMETER;

				uint32_t info [ 9 ] = { 0, graphics_modes [ mode ].width, graphics_modes [ mode ].height, 1, 0, 0, 0, 0, graphics_modes [ mode ].width };
				uint64_t buffer = pool ( MEMORY_BOOT_DATA, 36 );

				put ( cpu, buffer, info, 36 );
				put64 ( cpu, size_at, 36 );
				put64 ( cpu, info_at, buffer );

				return SUCCESS;

			}

			case GOP_SET:
				return set_graphics ( static_cast < uint32_t > ( argument ( cpu, 1 ) ) ) ? SUCCESS : UNSUPPORTED;

			case GOP_BLT:
				return blt ( cpu );

			case BLOCK_RESET:
			case BLOCK_FLUSH:
				return SUCCESS;

			case BLOCK_READ:
			case BLOCK_WRITE: {

				block_device* device = find_block ( argument ( cpu, 0 ), false );
				uint64_t lba = argument ( cpu, 2 );
				uint64_t size = argument ( cpu, 3 );
				uint64_t buffer = argument ( cpu, 4 );

				if ( !device )
					return INVALID_PARAMETER;

				if ( size % device->block_size )
					return BAD_BUFFER_SIZE;

				if ( lba > device->blocks || size / device->block_size > device->blocks - lba || ( size && !buffer ) )
					return INVALID_PARAMETER;

				if ( id == BLOCK_WRITE && device->read_only )
					return WRITE_PROTECTED;

				return size ? block_transfer ( cpu, *device, lba * device->block_size, size, buffer, id == BLOCK_WRITE ) : SUCCESS;

			}

			case DISK_READ:
			case DISK_WRITE: {

				block_device* device = find_block ( argument ( cpu, 0 ), true );
				uint64_t offset = argument ( cpu, 2 );
				uint64_t size = argument ( cpu, 3 );
				uint64_t buffer = argument ( cpu, 4 );
				uint64_t total = device ? device->blocks * device->block_size : 0;

				if ( !device || offset > total || size > total - offset || ( size && !buffer ) )
					return INVALID_PARAMETER;

				if ( id == DISK_WRITE && device->read_only )
					return WRITE_PROTECTED;

				return size ? block_transfer ( cpu, *device, offset, size, buffer, id == DISK_WRITE ) : SUCCESS;

			}

			default:
				handled = true;
				return UNSUPPORTED;

		}

	}

	void uefi::set_variable ( const char* name, const uint8_t* guid, uint32_t attributes, const void* data, uint32_t size ) {

		variable* found = nullptr;
		uint32_t length = 0;

		while ( name [ length ] )
			length++;

		for ( variable& item : variables ) {

			if ( item.name.size ( ) != length + 1 || !same_guid ( item.guid, guid ) )
				continue;

			bool same = true;

			for ( uint32_t i = 0; i < length && same; i++ )
				same = item.name [ i ] == static_cast < uint16_t > ( name [ i ] );

			if ( same )
				found = &item;

		}

		if ( !found ) {

			variable fresh;

			for ( uint32_t i = 0; i <= length; i++ )
				fresh.name.push ( static_cast < uint16_t > ( i < length ? name [ i ] : 0 ) );

			for ( int32_t i = 0; i < 16; i++ )
				fresh.guid [ i ] = guid [ i ];

			variables.push ( base::move ( fresh ) );
			found = &variables.back ( );

		}

		found->attributes = attributes;
		found->data.resize ( size );

		for ( uint32_t i = 0; i < size; i++ )
			found->data [ i ] = static_cast < const uint8_t* > ( data ) [ i ];

	}

	void uefi::load_variables ( ) {

		base::list < char > raw;

		if ( !link.variables || !files::read ( link.variables, raw ) )
			return;

		const uint8_t* bytes = reinterpret_cast < const uint8_t* > ( raw.data ( ) );
		uint64_t size = raw.size ( );

		for ( uint64_t at = 0; at + 28 <= size; ) {

			uint32_t name_length = le32 ( bytes + at );
			uint32_t attributes = le32 ( bytes + at + 4 + 16 );
			uint32_t data_size = le32 ( bytes + at + 24 );

			if ( name_length > 1024 || data_size > 0x10000 || at + 28 + name_length * 2ull + data_size > size )
				break;

			variable item;

			for ( uint32_t i = 0; i < 16; i++ )
				item.guid [ i ] = bytes [ at + 4 + i ];

			item.attributes = attributes;

			for ( uint32_t i = 0; i < name_length; i++ )
				item.name.push ( static_cast < uint16_t > ( bytes [ at + 28 + i * 2 ] | bytes [ at + 29 + i * 2 ] << 8 ) );

			for ( uint32_t i = 0; i < data_size; i++ )
				item.data.push ( bytes [ at + 28 + name_length * 2ull + i ] );

			variables.push ( base::move ( item ) );
			at += 28 + name_length * 2ull + data_size;

		}

	}

	void uefi::save_variables ( ) {

		if ( !link.variables )
			return;

		base::list < char > out;

		for ( const variable& item : variables ) {

			if ( !( item.attributes & 1 ) )
				continue;

			uint8_t head [ 28 ];

			put_u32 ( head, static_cast < uint32_t > ( item.name.size ( ) ) );

			for ( int32_t i = 0; i < 16; i++ )
				head [ 4 + i ] = item.guid [ i ];

			put_u32 ( head + 20, item.attributes );
			put_u32 ( head + 24, static_cast < uint32_t > ( item.data.size ( ) ) );

			for ( uint8_t value : head )
				out.push ( static_cast < char > ( value ) );

			for ( uint16_t value : item.name ) {

				out.push ( static_cast < char > ( value ) );
				out.push ( static_cast < char > ( value >> 8 ) );

			}

			for ( uint8_t value : item.data )
				out.push ( static_cast < char > ( value ) );

		}

		files::write ( link.variables, out.data ( ), out.count ( ) );

	}

	void uefi::build_variables ( ) {

		load_variables ( );

		static const char language [ ] = "en-US";
		static const char lang [ ] = "eng";
		uint8_t zero = 0;
		uint8_t one = 1;
		uint64_t none = 0;
		uint16_t wait = 0;

		set_variable ( "PlatformLang", guid_global, 0x07, language, 6 );
		set_variable ( "PlatformLangCodes", guid_global, 0x06, language, 6 );
		set_variable ( "Lang", guid_global, 0x07, lang, 4 );
		set_variable ( "LangCodes", guid_global, 0x06, lang, 4 );
		set_variable ( "SecureBoot", guid_global, 0x06, &zero, 1 );
		set_variable ( "SetupMode", guid_global, 0x06, &one, 1 );
		set_variable ( "OsIndicationsSupported", guid_global, 0x06, &none, 8 );

		bool timed = false;

		for ( const variable& item : variables ) {

			static const char name [ ] = "Timeout";
			bool same = item.name.size ( ) == 8;

			for ( uint32_t i = 0; i < 7 && same; i++ )
				same = item.name [ i ] == static_cast < uint16_t > ( name [ i ] );

			timed = timed || same;

		}

		if ( !timed )
			set_variable ( "Timeout", guid_global, 0x07, &wait, 2 );

	}

	uint64_t uefi::runtime_service ( uint32_t id, hv::cpu_state& cpu ) {

		switch ( id ) {

			case RT_GET_TIME: {

				uint64_t out = argument ( cpu, 0 );
				uint64_t capabilities = argument ( cpu, 1 );
				SYSTEMTIME now;

				if ( !out )
					return INVALID_PARAMETER;

				GetLocalTime ( &now );

				uint8_t time [ 16 ] = { static_cast < uint8_t > ( now.wYear ), static_cast < uint8_t > ( now.wYear >> 8 ), static_cast < uint8_t > ( now.wMonth ), static_cast < uint8_t > ( now.wDay ), static_cast < uint8_t > ( now.wHour ), static_cast < uint8_t > ( now.wMinute ), static_cast < uint8_t > ( now.wSecond ), 0 };

				put_u32 ( time + 8, now.wMilliseconds * 1000000u );
				time [ 12 ] = 0xFF;
				time [ 13 ] = 0x07;
				put ( cpu, out, time, 16 );

				if ( capabilities ) {

					uint8_t caps [ 12 ] = { 1, 0, 0, 0, 0x80, 0xF0, 0xFA, 0x02 };

					put ( cpu, capabilities, caps, 9 );

				}

				return SUCCESS;

			}

			case RT_SET_TIME:
				return SUCCESS;

			case RT_VIRTUAL_MAP: {

				uint64_t size = argument ( cpu, 0 );
				uint64_t descriptor = argument ( cpu, 1 );
				uint64_t map = argument ( cpu, 3 );

				if ( virtual_mode || !exited )
					return UNSUPPORTED;

				if ( descriptor < 40 || !map || size > 0x100000 )
					return INVALID_PARAMETER;

				uint8_t* table = static_cast < uint8_t* > ( runtime_allocate ( size ) );

				get ( cpu, map, table, size );

				for ( uint64_t at = 0; at + descriptor <= size; at += descriptor ) {

					if ( !( le64 ( table + at + 32 ) & 0x8000000000000000ull ) )
						continue;

					mapping item;

					item.physical = le64 ( table + at + 8 );
					item.virtual_start = le64 ( table + at + 16 );
					item.pages = le64 ( table + at + 24 );
					mappings.push ( item );

				}

				runtime_release ( table );

				auto convert = [ & ] ( uint64_t physical ) {

					for ( const mapping& item : mappings ) {

						if ( physical >= item.physical && physical < item.physical + item.pages * page )
							return item.virtual_start + ( physical - item.physical );

					}

					return physical;

				};

				for ( uint32_t i = RT_GET_TIME; i < RUNTIME_SERVICE_END; i++ )
					poke64 ( runtime_table + 24 + ( i - RT_GET_TIME ) * 8ull, convert ( stub ( i ) ) );

				uint8_t services [ 136 ];

				poke32 ( runtime_table + 16, 0 );
				vm->read ( runtime_table, services, 136 );
				poke32 ( runtime_table + 16, crc32 ( services, 136, 0 ) );
				poke64 ( system_table + 24, convert ( vendor ) );
				poke64 ( system_table + 88, convert ( runtime_table ) );
				poke64 ( system_table + 112, convert ( config_table ) );

				uint8_t header [ 120 ];

				poke32 ( system_table + 16, 0 );
				vm->read ( system_table, header, 120 );
				poke32 ( system_table + 16, crc32 ( header, 120, 0 ) );
				virtual_mode = true;
				log ( "SetVirtualAddressMap\n" );

				return SUCCESS;

			}

			case RT_CONVERT_POINTER: {

				uint64_t where = argument ( cpu, 1 );

				if ( !where )
					return INVALID_PARAMETER;

				uint64_t value = get64 ( cpu, where );

				for ( const mapping& item : mappings ) {

					if ( value >= item.physical && value < item.physical + item.pages * page ) {

						put64 ( cpu, where, item.virtual_start + ( value - item.physical ) );

						return SUCCESS;

					}

				}

				return NOT_FOUND;

			}

			case RT_GET_VARIABLE:
			case RT_SET_VARIABLE: {

				uint64_t name_at = argument ( cpu, 0 );
				uint64_t guid_at = argument ( cpu, 1 );
				uint16_t name [ 512 ] = { };
				uint8_t guid [ 16 ];
				uint32_t length = 0;

				if ( !name_at || !guid_at )
					return INVALID_PARAMETER;

				get ( cpu, guid_at, guid, 16 );

				for ( ; length < 511; length++ ) {

					get ( cpu, name_at + length * 2ull, &name [ length ], 2 );

					if ( !name [ length ] )
						break;

				}

				int32_t index = -1;

				for ( int32_t i = 0; i < variables.count ( ) && index < 0; i++ ) {

					const variable& item = variables [ i ];

					if ( item.name.size ( ) != length + 1 || !same_guid ( item.guid, guid ) )
						continue;

					bool same = true;

					for ( uint32_t c = 0; c < length && same; c++ )
						same = item.name [ c ] == name [ c ];

					if ( same )
						index = i;

				}

				if ( id == RT_GET_VARIABLE ) {

					uint64_t attributes_at = argument ( cpu, 2 );
					uint64_t size_at = argument ( cpu, 3 );
					uint64_t data_at = argument ( cpu, 4 );

					if ( !size_at )
						return INVALID_PARAMETER;

					if ( index < 0 || ( exited && !( variables [ index ].attributes & 4 ) ) )
						return NOT_FOUND;

					const variable& item = variables [ index ];
					uint64_t room = get64 ( cpu, size_at );

					put64 ( cpu, size_at, item.data.size ( ) );

					if ( attributes_at )
						put32 ( cpu, attributes_at, item.attributes );

					if ( room < item.data.size ( ) || ( item.data.size ( ) && !data_at ) )
						return BUFFER_TOO_SMALL;

					put ( cpu, data_at, item.data.data ( ), item.data.size ( ) );

					return SUCCESS;

				}

				uint32_t attributes = static_cast < uint32_t > ( argument ( cpu, 2 ) );
				uint64_t size = argument ( cpu, 3 );
				uint64_t data_at = argument ( cpu, 4 );
				bool append = ( attributes & 0x40 ) != 0;

				if ( size > 0x10000 || ( size && !data_at ) )
					return INVALID_PARAMETER;

				if ( exited && attributes && !( attributes & 4 ) )
					return INVALID_PARAMETER;

				if ( ( !size && !append ) || !( attributes & 7 ) ) {

					if ( index < 0 )
						return NOT_FOUND;

					bool lasting = ( variables [ index ].attributes & 1 ) != 0;

					variables.remove_at ( static_cast < size_t > ( index ) );

					if ( lasting )
						save_variables ( );

					return SUCCESS;

				}

				base::list < uint8_t > data;

				if ( append && index >= 0 )
					data = variables [ index ].data;

				uint64_t start = data.size ( );

				data.resize ( start + size );

				if ( size )
					get ( cpu, data_at, data.data ( ) + start, size );

				if ( index < 0 ) {

					variable fresh;

					for ( uint32_t c = 0; c <= length; c++ )
						fresh.name.push ( name [ c ] );

					for ( int32_t b = 0; b < 16; b++ )
						fresh.guid [ b ] = guid [ b ];

					variables.push ( base::move ( fresh ) );
					index = variables.count ( ) - 1;

				}

				variables [ index ].attributes = attributes & 0x0F;
				variables [ index ].data = base::move ( data );

				if ( attributes & 1 )
					save_variables ( );

				return SUCCESS;

			}

			case RT_NEXT_VARIABLE: {

				uint64_t size_at = argument ( cpu, 0 );
				uint64_t name_at = argument ( cpu, 1 );
				uint64_t guid_at = argument ( cpu, 2 );

				if ( !size_at || !name_at || !guid_at )
					return INVALID_PARAMETER;

				uint16_t name [ 512 ] = { };
				uint8_t guid [ 16 ];
				uint32_t length = 0;

				get ( cpu, guid_at, guid, 16 );

				for ( ; length < 511; length++ ) {

					get ( cpu, name_at + length * 2ull, &name [ length ], 2 );

					if ( !name [ length ] )
						break;

				}

				int32_t next = 0;

				if ( length ) {

					next = -1;

					for ( int32_t i = 0; i < variables.count ( ) && next < 0; i++ ) {

						const variable& item = variables [ i ];

						if ( item.name.size ( ) != length + 1 || !same_guid ( item.guid, guid ) )
							continue;

						bool same = true;

						for ( uint32_t c = 0; c < length && same; c++ )
							same = item.name [ c ] == name [ c ];

						if ( same )
							next = i + 1;

					}

					if ( next < 0 )
						return INVALID_PARAMETER;

				}

				while ( next < variables.count ( ) && exited && !( variables [ next ].attributes & 4 ) )
					next++;

				if ( next >= variables.count ( ) )
					return NOT_FOUND;

				const variable& item = variables [ next ];
				uint64_t needed = item.name.size ( ) * 2;
				uint64_t room = get64 ( cpu, size_at );

				put64 ( cpu, size_at, needed );

				if ( room < needed )
					return BUFFER_TOO_SMALL;

				put ( cpu, name_at, item.name.data ( ), needed );
				put ( cpu, guid_at, item.guid, 16 );

				return SUCCESS;

			}

			case RT_HIGH_COUNT:

				if ( argument ( cpu, 0 ) )
					put32 ( cpu, argument ( cpu, 0 ), static_cast < uint32_t > ( ++monotonic >> 32 ) + 1 );

				return SUCCESS;

			case RT_VARIABLE_INFO: {

				uint64_t values [ 3 ] = { 0x10000, 0x8000, 0x4000 };

				for ( uint32_t i = 0; i < 3; i++ ) {

					if ( argument ( cpu, i + 1 ) )
						put64 ( cpu, argument ( cpu, i + 1 ), values [ i ] );

				}

				return SUCCESS;

			}

			default:
				return UNSUPPORTED;

		}

	}

}