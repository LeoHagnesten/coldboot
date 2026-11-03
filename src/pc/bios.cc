#include "pc/bios.hh"
#include "assembler/assembler.hh"
#include "pc/acpi.hh"
#include "pc/smbios.hh"
#include "video/cga.hh"
#include <windows.h>

namespace pc {

	static constexpr uint32_t rom_base = 0xF0000;
	static constexpr uint32_t video_rom = 0xC0000;
	static constexpr uint32_t font_16 = 0x1000;
	static constexpr uint32_t font_8 = 0x2000;
	static constexpr uint32_t font_14 = 0x2800;
	static constexpr uint32_t text_oem = 0x4000;
	static constexpr uint32_t text_vendor = 0x4020;
	static constexpr uint32_t text_product = 0x4040;
	static constexpr uint32_t text_revision = 0x4060;
	static constexpr uint32_t mode_list = 0x4100;
	static constexpr uint32_t mouse_handler = 0x20;
	static constexpr uint32_t mouse_packet = 0x24;

	static const char normal_keys [ 0x59 ] = {

		0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9,
		'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13, 0, 'a', 's',
		'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
		'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
		'2', '3', '0', '.', 0, 0, '\\', 0, 0,

	};

	static const char shifted_keys [ 0x59 ] = {

		0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8, 0,
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 13, 0, 'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
		'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-', 0, 0, 0, '+', 0,
		0, 0, 0, 0, 0, 0, '|', 0, 0,

	};

	static const char* resource_text ( int32_t id, int32_t& size ) {

		HMODULE module = GetModuleHandleW ( nullptr );
		HRSRC found = FindResourceW ( module, MAKEINTRESOURCEW ( id ), MAKEINTRESOURCEW ( 10 ) );

		size = 0;

		if ( !found )
			return nullptr;

		HGLOBAL loaded = LoadResource ( module, found );

		if ( !loaded )
			return nullptr;

		size = static_cast < int32_t > ( SizeofResource ( module, found ) );

		return static_cast < const char* > ( LockResource ( loaded ) );

	}

	static void copy_text ( char* out, const char* text, int32_t room ) {

		int32_t used = 0;

		for ( ; text [ used ] && used < room - 1; used++ )
			out [ used ] = text [ used ];

		out [ used ] = 0;

	}

	uint8_t* bios::memory ( uint32_t address, uint32_t size ) {

		return link.vm->core.host ( address, size );

	}

	uint8_t bios::peek8 ( uint32_t address ) {

		const uint8_t* at = memory ( address, 1 );

		return at ? at [ 0 ] : 0;

	}

	uint16_t bios::peek16 ( uint32_t address ) {

		return static_cast < uint16_t > ( peek8 ( address ) | peek8 ( address + 1 ) << 8 );

	}

	uint32_t bios::peek32 ( uint32_t address ) {

		return static_cast < uint32_t > ( peek16 ( address ) ) | static_cast < uint32_t > ( peek16 ( address + 2 ) ) << 16;

	}

	void bios::poke8 ( uint32_t address, uint8_t value ) {

		uint8_t* at = memory ( address, 1 );

		if ( at )
			at [ 0 ] = value;

	}

	void bios::poke16 ( uint32_t address, uint16_t value ) {

		poke8 ( address, static_cast < uint8_t > ( value ) );
		poke8 ( address + 1, static_cast < uint8_t > ( value >> 8 ) );

	}

	void bios::poke32 ( uint32_t address, uint32_t value ) {

		poke16 ( address, static_cast < uint16_t > ( value ) );
		poke16 ( address + 2, static_cast < uint16_t > ( value >> 16 ) );

	}

	void bios::copy_in ( uint32_t address, const void* data, uint32_t size ) {

		const uint8_t* bytes = static_cast < const uint8_t* > ( data );

		for ( uint32_t i = 0; i < size; i++ )
			poke8 ( address + i, bytes [ i ] );

	}

	void bios::copy_out ( uint32_t address, void* out, uint32_t size ) {

		uint8_t* bytes = static_cast < uint8_t* > ( out );

		for ( uint32_t i = 0; i < size; i++ )
			bytes [ i ] = peek8 ( address + i );

	}

	void bios::set_vector ( uint8_t vector, uint16_t segment, uint16_t offset ) {

		poke16 ( vector * 4u, offset );
		poke16 ( vector * 4u + 2, segment );

	}

	bool bios::install ( const bios_links& links, char* error, int32_t room ) {

		link = links;
		frame_count = 0;
		frame_cursor = 0;
		request_count = 0;
		request_cursor = 0;
		input_wanted = 0;
		input_size = 0;
		output_size = 0;
		emulating = false;
		boot_media = 0;
		mouse_enabled = false;
		mouse_count = 0;
		extended_prefix = false;
		last_boot [ 0 ] = 0;

		int32_t size = 0;
		const char* source = resource_text ( 108, size );

		if ( !source ) {

			copy_text ( error, "The modern BIOS is missing from the program's resources", room );

			return false;

		}

		base::list < char > text;

		for ( int32_t i = 0; i < size; i++ ) {

			if ( source [ i ] != '\r' )
				text.push ( source [ i ] );

		}

		text.push ( '\n' );

		assembler::result* image = new assembler::result ( );

		if ( !assembler::assemble ( text.data ( ), text.count ( ), nullptr, 0, *image ) ) {

			copy_text ( error, image->error, room );
			delete image;

			return false;

		}

		if ( image->origin != 0xE000 || image->bytes.count ( ) != 0x2000 ) {

			copy_text ( error, "The modern BIOS has to fill F000:E000 to F000:FFFF exactly", room );
			delete image;

			return false;

		}

		static const char* const names [ ] = {

			"vector_09", "vector_10", "vector_11", "vector_12", "vector_13", "vector_14", "vector_15", "vector_16",
			"vector_17", "vector_18", "vector_19", "vector_1a", "vector_74", "vector_08", "vector_70", "irq_master",
			"irq_slave", "dummy_iret", "entry", "no_boot", "config_table", "disk_table", "video_table",

		};

		uint32_t found [ 23 ] = { };

		for ( int32_t i = 0; i < 23; i++ ) {

			if ( !assembler::find ( *image, names [ i ], found [ i ] ) ) {

				copy_text ( error, "The modern BIOS lost one of its entry points", room );
				delete image;

				return false;

			}

		}

		copy_in ( rom_base + image->origin, image->bytes.data ( ), 0x2000 );
		delete image;

		for ( int32_t i = 0; i < 18; i++ )
			vector_offsets [ i ] = found [ i ];

		dummy_offset = found [ 17 ];
		entry_offset = found [ 18 ];
		no_boot_offset = found [ 19 ];
		config_offset = found [ 20 ];
		disk_table_offset = found [ 21 ];
		video_table_offset = found [ 22 ];

		build_video_rom ( );
		reset_chips ( );
		reset_bda ( );
		build_tables ( );

		return true;

	}

	void bios::build_video_rom ( ) {

		uint8_t* rom = memory ( video_rom, 0x8000 );

		if ( !rom )
			return;

		for ( uint32_t i = 0; i < 0x8000; i++ )
			rom [ i ] = 0;

		rom [ 0 ] = 0x55;
		rom [ 1 ] = 0xAA;
		rom [ 2 ] = 0x40;
		rom [ 3 ] = 0xCB;

		for ( int32_t c = 0; c < 256; c++ ) {

			for ( int32_t line = 0; line < 16; line++ )
				rom [ font_16 + c * 16 + line ] = machine::font_8x8 [ c ] [ line / 2 ];

			for ( int32_t line = 0; line < 8; line++ )
				rom [ font_8 + c * 8 + line ] = machine::font_8x8 [ c ] [ line ];

			for ( int32_t line = 0; line < 14; line++ )
				rom [ font_14 + c * 14 + line ] = machine::font_8x8 [ c ] [ line * 8 / 14 ];

		}

		const char* strings [ 4 ] = { "Coldboot VGA", "Coldboot", "Coldboot Display", "1.0" };
		uint32_t places [ 4 ] = { text_oem, text_vendor, text_product, text_revision };

		for ( int32_t s = 0; s < 4; s++ ) {

			for ( int32_t i = 0; strings [ s ] [ i ]; i++ )
				rom [ places [ s ] + i ] = static_cast < uint8_t > ( strings [ s ] [ i ] );

		}

		uint32_t at = mode_list;

		for ( int32_t i = 0; i < vga::mode_count; i++ ) {

			rom [ at++ ] = static_cast < uint8_t > ( vga::modes [ i ].number );
			rom [ at++ ] = static_cast < uint8_t > ( vga::modes [ i ].number >> 8 );

		}

		rom [ at++ ] = 0xFF;
		rom [ at++ ] = 0xFF;

		uint8_t sum = 0;

		for ( uint32_t i = 0; i < 0x7FFF; i++ )
			sum = static_cast < uint8_t > ( sum + rom [ i ] );

		rom [ 0x7FFF ] = static_cast < uint8_t > ( 0x100 - sum );

	}

	void bios::reset_chips ( ) {

		dual_pic& pics = *link.pics;

		pics.write ( 0x20, 1, 0x11 );
		pics.write ( 0x21, 1, 0x08 );
		pics.write ( 0x21, 1, 0x04 );
		pics.write ( 0x21, 1, 0x01 );
		pics.write ( 0x21, 1, 0xB8 );
		pics.write ( 0xA0, 1, 0x11 );
		pics.write ( 0xA1, 1, 0x70 );
		pics.write ( 0xA1, 1, 0x02 );
		pics.write ( 0xA1, 1, 0x01 );
		pics.write ( 0xA1, 1, 0xFF );
		pics.write ( 0x4D0, 1, 0x00 );
		pics.write ( 0x4D1, 1, 0x02 );

		if ( link.timer ) {

			link.timer->write ( 0x43, 1, 0x36 );
			link.timer->write ( 0x40, 1, 0x00 );
			link.timer->write ( 0x40, 1, 0x00 );

		}

		hv::machine& vm = *link.vm;

		vm.set_mtrr ( 0x2FF, 0xC06 );
		vm.set_mtrr ( 0x250, 0x0606060606060606ull );
		vm.set_mtrr ( 0x258, 0x0606060606060606ull );
		vm.set_mtrr ( 0x259, 0 );

		for ( uint32_t msr = 0x268; msr <= 0x26F; msr++ )
			vm.set_mtrr ( msr, 0x0505050505050505ull );

		vm.set_mtrr ( 0x200, 0xC0000000ull );
		vm.set_mtrr ( 0x201, 0xFFFFC0000800ull );

	}

	void bios::reset_bda ( ) {

		uint8_t* low = memory ( 0, 0x500 );

		for ( uint32_t i = 0; low && i < 0x500; i++ )
			low [ i ] = 0;

		uint8_t* extended = memory ( ebda, 0x400 );

		for ( uint32_t i = 0; extended && i < 0x400; i++ )
			extended [ i ] = 0;

		poke8 ( ebda, 1 );

		for ( uint32_t v = 0; v < 256; v++ )
			set_vector ( static_cast < uint8_t > ( v ), 0xF000, static_cast < uint16_t > ( dummy_offset ) );

		static const uint8_t services [ 13 ] = { 0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x74 };

		for ( int32_t i = 0; i < 13; i++ )
			set_vector ( services [ i ], 0xF000, static_cast < uint16_t > ( vector_offsets [ i ] ) );

		set_vector ( 0x08, 0xF000, static_cast < uint16_t > ( vector_offsets [ 13 ] ) );
		set_vector ( 0x70, 0xF000, static_cast < uint16_t > ( vector_offsets [ 14 ] ) );
		set_vector ( 0x40, 0xF000, static_cast < uint16_t > ( vector_offsets [ 4 ] ) );

		for ( uint8_t v = 0x0A; v <= 0x0F; v++ )
			set_vector ( v, 0xF000, static_cast < uint16_t > ( vector_offsets [ 15 ] ) );

		for ( uint8_t v = 0x71; v <= 0x77; v++ ) {

			if ( v != 0x74 )
				set_vector ( v, 0xF000, static_cast < uint16_t > ( vector_offsets [ 16 ] ) );

		}

		set_vector ( 0x1D, 0xF000, static_cast < uint16_t > ( video_table_offset ) );
		set_vector ( 0x1E, 0xF000, static_cast < uint16_t > ( disk_table_offset ) );
		set_vector ( 0x1F, 0xC000, static_cast < uint16_t > ( font_8 + 0x400 ) );
		set_vector ( 0x43, 0xC000, static_cast < uint16_t > ( font_8 ) );
		set_vector ( 0x41, static_cast < uint16_t > ( ebda >> 4 ), 0x3D );
		set_vector ( 0x46, static_cast < uint16_t > ( ebda >> 4 ), 0x4D );

		for ( uint8_t v = 0x60; v <= 0x67; v++ )
			set_vector ( v, 0, 0 );

		poke16 ( 0x400, 0x3F8 );
		poke16 ( 0x40E, static_cast < uint16_t > ( ebda >> 4 ) );
		poke16 ( 0x410, 0x0226 );
		poke16 ( 0x413, static_cast < uint16_t > ( ebda / 1024 ) );
		poke16 ( 0x41A, 0x1E );
		poke16 ( 0x41C, 0x1E );
		poke16 ( 0x480, 0x1E );
		poke16 ( 0x482, 0x3E );
		poke8 ( 0x475, static_cast < uint8_t > ( link.hard_disk && link.hard_disk->present ( ) ? 1 : 0 ) );
		poke8 ( 0x496, 0x10 );

		if ( link.hard_disk && link.hard_disk->present ( ) ) {

			uint64_t sectors = link.hard_disk->size / 512;
			uint32_t cylinders = static_cast < uint32_t > ( base::min ( sectors / ( 255 * 63 ), static_cast < uint64_t > ( 1024 ) ) );

			poke16 ( ebda + 0x3D, static_cast < uint16_t > ( cylinders ) );
			poke8 ( ebda + 0x3D + 2, 255 );
			poke8 ( ebda + 0x3D + 14, 63 );

		}

		set_mode ( 3, true );

	}

	void bios::build_tables ( ) {

		hv::machine& vm = *link.vm;
		acpi_layout layout;
		acpi_image* image = new acpi_image ( );

		layout.processors = link.processors;
		layout.low_memory = vm.core.low_size;
		layout.high_memory = vm.core.high_size ( );
		layout.tables = static_cast < uint32_t > ( vm.core.low_size - firmware_room );
		build_acpi ( layout, *image );
		copy_in ( layout.tables, image->tables.data ( ), static_cast < uint32_t > ( image->tables.size ( ) ) );
		copy_in ( 0xE0000, image->rsdp, 36 );
		delete image;

		smbios_layout shape;
		smbios_image* table = new smbios_image ( );

		shape.processors = link.processors;
		shape.low_memory = vm.core.low_size;
		shape.high_memory = vm.core.high_size ( );
		shape.seed = link.seed;
		build_smbios ( shape, *table );
		copy_in ( 0xF0000, table->entry, 31 );
		copy_in ( shape.table_address, table->table.data ( ), static_cast < uint32_t > ( table->table.size ( ) ) );
		delete table;

	}

	uint64_t bios::read ( uint64_t address, uint32_t ) {

		uint32_t offset = static_cast < uint32_t > ( address - port );

		if ( offset == 4 )
			return frame_cursor < 19 ? frame_words [ frame_cursor++ ] : 0;

		if ( offset == 6 )
			return request_cursor < request_count ? requests [ request_cursor++ ] : 0;

		if ( offset == 8 ) {

			uint32_t at = output_cursor++;

			return at < output_size ? output [ at ] : 0;

		}

		return 0;

	}

	void bios::write ( uint64_t address, uint32_t, uint64_t value ) {

		uint32_t offset = static_cast < uint32_t > ( address - port );
		uint16_t word = static_cast < uint16_t > ( value );

		if ( offset == 0 )
			execute ( word );
		else if ( offset == 4 ) {

			if ( frame_count < 19 )
				frame_words [ frame_count++ ] = word;

		} else if ( offset == 8 ) {

			if ( input_size < transfer_room )
				input [ input_size++ ] = static_cast < uint8_t > ( word );

		}

	}

	void bios::execute ( uint16_t vector ) {

		if ( frame_count == 19 ) {

			bios_frame& frame = frame_state;

			frame.es = frame_words [ 0 ];
			frame.ds = frame_words [ 1 ];
			frame.ebp = frame_words [ 2 ] | static_cast < uint32_t > ( frame_words [ 3 ] ) << 16;
			frame.edi = frame_words [ 4 ] | static_cast < uint32_t > ( frame_words [ 5 ] ) << 16;
			frame.esi = frame_words [ 6 ] | static_cast < uint32_t > ( frame_words [ 7 ] ) << 16;
			frame.edx = frame_words [ 8 ] | static_cast < uint32_t > ( frame_words [ 9 ] ) << 16;
			frame.ecx = frame_words [ 10 ] | static_cast < uint32_t > ( frame_words [ 11 ] ) << 16;
			frame.eax = frame_words [ 12 ] | static_cast < uint32_t > ( frame_words [ 13 ] ) << 16;
			frame.ebx = frame_words [ 14 ] | static_cast < uint32_t > ( frame_words [ 15 ] ) << 16;
			frame.ip = frame_words [ 16 ];
			frame.cs = frame_words [ 17 ];
			frame.flags = frame_words [ 18 ];
			input_ready = false;
			input_size = 0;

		} else
			input_ready = input_wanted > 0;

		frame_count = 0;
		request_count = 0;
		request_cursor = 0;
		output_size = 0;
		output_cursor = 0;

		bios_frame working = frame_state;
		uint32_t result = dispatch ( vector, working );

		if ( result == status_needs_input ) {

			input_size = 0;
			requests [ request_count++ ] = 1;
			requests [ request_count++ ] = input_segment;
			requests [ request_count++ ] = input_offset;
			requests [ request_count++ ] = static_cast < uint16_t > ( input_wanted );

			return;

		}

		input_wanted = 0;
		input_ready = false;

		if ( result == 1 ) {

			requests [ request_count++ ] = 3;

			return;

		}

		frame_state = working;

		if ( output_size ) {

			requests [ request_count++ ] = 2;
			requests [ request_count++ ] = output_segment;
			requests [ request_count++ ] = output_offset;
			requests [ request_count++ ] = static_cast < uint16_t > ( output_size );

		}

		requests [ request_count++ ] = static_cast < uint16_t > ( result == 2 ? 4 : 0 );

		const bios_frame& frame = frame_state;
		uint32_t values [ 7 ] = { frame.ebp, frame.edi, frame.esi, frame.edx, frame.ecx, frame.eax, frame.ebx };

		frame_words [ 0 ] = frame.es;
		frame_words [ 1 ] = frame.ds;

		for ( int32_t i = 0; i < 7; i++ ) {

			frame_words [ 2 + i * 2 ] = static_cast < uint16_t > ( values [ i ] );
			frame_words [ 3 + i * 2 ] = static_cast < uint16_t > ( values [ i ] >> 16 );

		}

		frame_words [ 16 ] = frame.ip;
		frame_words [ 17 ] = frame.cs;
		frame_words [ 18 ] = frame.flags;
		frame_cursor = 0;

	}

	bool bios::need ( uint16_t segment, uint16_t offset, uint32_t bytes ) {

		if ( !bytes )
			return true;

		if ( input_ready && input_size >= bytes )
			return true;

		input_wanted = bytes < 0xFFFF ? bytes : 0xFFFF;
		input_segment = segment;
		input_offset = offset;

		return false;

	}

	void bios::give ( uint16_t segment, uint16_t offset, const void* data, uint32_t bytes ) {

		const uint8_t* source = static_cast < const uint8_t* > ( data );

		if ( bytes > 0xFFFF )
			bytes = 0xFFFF;

		for ( uint32_t i = 0; i < bytes; i++ )
			output [ i ] = source [ i ];

		output_size = bytes;
		output_segment = segment;
		output_offset = offset;

	}

	uint32_t bios::dispatch ( uint16_t vector, bios_frame& frame ) {

		switch ( vector ) {

			case 0x09:
				keyboard_interrupt ( );
				return 0;

			case 0x10:
				return video ( frame );

			case 0x11:
				frame.set_ax ( peek16 ( 0x410 ) );
				return 0;

			case 0x12:
				frame.set_ax ( peek16 ( 0x413 ) );
				return 0;

			case 0x13:
				return disk ( frame );

			case 0x14:

				if ( frame.ah ( ) == 0x01 && link.bus )
					link.bus->console ( static_cast < char > ( frame.al ( ) ) );

				frame.set_ax ( static_cast < uint16_t > ( frame.ah ( ) == 0x02 ? 0x8000 : 0x6030 ) );
				return 0;

			case 0x15:
				return system ( frame );

			case 0x16:
				return keyboard_service ( frame );

			case 0x17:
				frame.set_ah ( 0x08 );
				return 0;

			case 0x18:
				print ( "\r\nNo bootable medium. Insert a disc or attach a hard disk, then press any key.\r\n", 0x07 );
				frame.cs = 0xF000;
				frame.ip = static_cast < uint16_t > ( no_boot_offset );
				return 0;

			case 0x19:
				return boot ( frame );

			case 0x1A:
				return time ( frame );

			case 0x74:
				return mouse_interrupt ( );

			default:
				return 0;

		}

	}

	void bios::move_cursor ( uint8_t page, uint8_t row, uint8_t column ) {

		poke16 ( 0x450 + ( page & 7 ) * 2u, static_cast < uint16_t > ( row << 8 | column ) );

		if ( page != peek8 ( 0x462 ) )
			return;

		uint32_t offset = peek16 ( 0x44E ) / 2u + row * static_cast < uint32_t > ( peek16 ( 0x44A ) ) + column;

		link.video->crtc [ 0x0E ] = static_cast < uint8_t > ( offset >> 8 );
		link.video->crtc [ 0x0F ] = static_cast < uint8_t > ( offset );

	}

	void bios::set_mode ( uint8_t requested, bool clear_screen ) {

		vga& screen = *link.video;
		uint8_t mode = static_cast < uint8_t > ( requested & 0x7F );

		if ( mode > 0x13 || ( mode > 7 && mode < 0x0D ) )
			mode = 3;

		bool text = mode <= 3 || mode == 7;
		uint16_t columns = mode == 0 || mode == 1 || mode == 4 || mode == 5 || mode == 0x0D || mode == 0x13 ? 40 : 80;
		uint8_t rows = mode == 0x11 || mode == 0x12 ? 30 : 25;
		uint8_t height = text ? 16 : ( mode == 0x0F || mode == 0x10 ? 14 : ( mode == 0x11 || mode == 0x12 ? 16 : 8 ) );

		screen.set_font ( text ? 16 : height );
		screen.set_legacy ( mode );
		poke8 ( 0x449, mode );
		poke16 ( 0x44A, columns );
		poke16 ( 0x44C, static_cast < uint16_t > ( text ? ( columns == 40 ? 0x800 : 0x1000 ) : ( mode <= 6 ? 0x4000 : 0 ) ) );
		poke16 ( 0x44E, 0 );

		for ( uint32_t i = 0; i < 8; i++ )
			poke16 ( 0x450 + i * 2, 0 );

		poke16 ( 0x460, 0x0607 );
		poke8 ( 0x462, 0 );
		poke16 ( 0x463, static_cast < uint16_t > ( mode == 7 ? 0x3B4 : 0x3D4 ) );
		poke8 ( 0x465, 0x29 );
		poke8 ( 0x466, 0x30 );
		poke8 ( 0x484, static_cast < uint8_t > ( rows - 1 ) );
		poke16 ( 0x485, height );
		poke8 ( 0x487, static_cast < uint8_t > ( ( requested & 0x80 ) | 0x60 ) );
		poke8 ( 0x488, 0x09 );
		poke8 ( 0x489, 0x51 );
		poke8 ( 0x48A, 0x08 );
		screen.crtc [ 0x0A ] = 0x0D;
		screen.crtc [ 0x0B ] = 0x0E;
		screen.crtc [ 0x0E ] = 0;
		screen.crtc [ 0x0F ] = 0;

		if ( !clear_screen || ( requested & 0x80 ) )
			return;

		if ( text ) {

			uint8_t* cells = memory ( mode == 7 ? 0xB0000 : 0xB8000, 0x8000 );

			for ( uint32_t i = 0; cells && i < 0x8000; i += 2 ) {

				cells [ i ] = ' ';
				cells [ i + 1 ] = 0x07;

			}

		} else if ( screen.kind == VGA_PLANAR ) {

			for ( uint32_t i = 0; i < 4 * 0x10000; i++ )
				screen.planes [ i ] = 0;

		} else {

			uint8_t* pixels = memory ( 0xA0000, 0x20000 );

			for ( uint32_t i = 0; pixels && i < 0x20000; i++ )
				pixels [ i ] = 0;

		}

	}

	void bios::scroll ( bool up, uint8_t lines, uint8_t attribute, uint8_t top, uint8_t left, uint8_t bottom, uint8_t right ) {

		uint8_t mode = peek8 ( 0x449 );
		uint32_t columns = peek16 ( 0x44A );
		uint32_t rows = peek8 ( 0x484 ) + 1u;

		if ( right >= columns )
			right = static_cast < uint8_t > ( columns - 1 );

		if ( bottom >= rows )
			bottom = static_cast < uint8_t > ( rows - 1 );

		if ( top > bottom || left > right )
			return;

		uint32_t height = bottom - top + 1u;
		uint32_t width = right - left + 1u;

		if ( !lines || lines > height )
			lines = static_cast < uint8_t > ( height );

		if ( link.video->kind == VGA_PLANAR ) {

			vga& screen = *link.video;
			uint32_t cell = peek16 ( 0x485 );
			uint32_t pitch = screen.planar_pitch ( );

			for ( uint32_t step = 0; step < height; step++ ) {

				uint32_t row = up ? top + step : bottom - step;
				uint32_t source = up ? row + lines : row - lines;
				bool inside = up ? source <= bottom : ( row >= top + lines );

				for ( uint32_t line = 0; line < cell; line++ ) {

					uint32_t target_offset = ( row * cell + line ) * pitch + left;
					uint32_t source_offset = ( source * cell + line ) * pitch + left;

					for ( uint32_t p = 0; p < 4; p++ ) {

						uint8_t* plane = screen.planes + p * 0x10000;
						uint8_t fill = attribute >> p & 1 ? 0xFF : 0x00;

						for ( uint32_t x = 0; x < width; x++ )
							plane [ ( target_offset + x ) & 0xFFFF ] = inside ? plane [ ( source_offset + x ) & 0xFFFF ] : fill;

					}

				}

			}

			return;

		}

		if ( mode == 0x13 ) {

			uint8_t* pixels = memory ( 0xA0000, 64000 );

			if ( !pixels )
				return;

			for ( uint32_t step = 0; step < height; step++ ) {

				uint32_t row = up ? top + step : bottom - step;
				uint32_t source = up ? row + lines : row - lines;
				bool inside = up ? source <= bottom : ( source >= top && source <= bottom && row >= lines );

				for ( uint32_t line = 0; line < 8; line++ ) {

					uint8_t* target = pixels + ( row * 8 + line ) * 320 + left * 8;

					for ( uint32_t x = 0; x < width * 8; x++ )
						target [ x ] = inside ? pixels [ ( source * 8 + line ) * 320 + left * 8 + x ] : attribute;

				}

			}

			return;

		}

		if ( !( mode <= 3 || mode == 7 ) )
			return;

		uint32_t base = ( mode == 7 ? 0xB0000 : 0xB8000 ) + peek16 ( 0x44E );
		uint8_t* cells = memory ( base, rows * columns * 2 );

		if ( !cells )
			return;

		for ( uint32_t step = 0; step < height; step++ ) {

			uint32_t row = up ? top + step : bottom - step;
			bool inside = up ? row + lines <= bottom : row >= top + lines;
			uint32_t source = up ? row + lines : row - lines;

			for ( uint32_t column = left; column <= right; column++ ) {

				uint8_t* target = cells + ( row * columns + column ) * 2;

				if ( inside ) {

					target [ 0 ] = cells [ ( source * columns + column ) * 2 ];
					target [ 1 ] = cells [ ( source * columns + column ) * 2 + 1 ];

				} else {

					target [ 0 ] = ' ';
					target [ 1 ] = attribute;

				}

			}

		}

	}

	void bios::put_character ( uint8_t character, uint8_t attribute, uint8_t page, bool teletype, bool with_attribute ) {

		uint8_t mode = peek8 ( 0x449 );
		uint32_t columns = peek16 ( 0x44A );
		uint32_t rows = peek8 ( 0x484 ) + 1u;
		uint16_t position = peek16 ( 0x450 + ( page & 7 ) * 2u );
		uint32_t row = position >> 8;
		uint32_t column = position & 0xFF;
		bool text = mode <= 3 || mode == 7;

		if ( teletype ) {

			if ( character == 7 )
				return;

			if ( character == 8 ) {

				if ( column )
					column--;

			} else if ( character == 10 )
				row++;
			else if ( character == 13 )
				column = 0;

		}

		if ( !teletype || character >= 32 || ( character != 7 && character != 8 && character != 10 && character != 13 ) ) {

			if ( text ) {

				uint32_t base = ( mode == 7 ? 0xB0000 : 0xB8000 ) + page * static_cast < uint32_t > ( peek16 ( 0x44C ) );
				uint8_t* cell = memory ( base + ( row * columns + column ) * 2, 2 );

				if ( cell ) {

					cell [ 0 ] = character;

					if ( with_attribute )
						cell [ 1 ] = attribute;

				}

			} else if ( link.video->kind == VGA_PLANAR ) {

				vga& screen = *link.video;
				uint32_t cell = peek16 ( 0x485 );
				uint8_t color = attribute & 0x0F;
				bool flip = ( attribute & 0x80 ) != 0;

				for ( uint32_t line = 0; line < cell; line++ ) {

					uint8_t bits = screen.font [ character ] [ line < 32 ? line : 31 ];

					if ( cell == 8 )
						bits = machine::font_8x8 [ character ] [ line ];

					for ( uint32_t x = 0; x < 8; x++ ) {

						bool on = ( bits >> ( 7 - x ) & 1 ) != 0;

						if ( flip ) {

							if ( on )
								screen.put_pixel ( column * 8 + x, row * cell + line, color, true );

						} else
							screen.put_pixel ( column * 8 + x, row * cell + line, on ? color : 0, false );

					}

				}

			} else if ( mode == 0x13 ) {

				uint8_t* pixels = memory ( 0xA0000, 64000 );

				for ( uint32_t line = 0; pixels && line < 8 && row < 25 && column < 40; line++ ) {

					uint8_t bits = machine::font_8x8 [ character ] [ line ];

					for ( uint32_t x = 0; x < 8; x++ ) {

						uint8_t* target = pixels + ( row * 8 + line ) * 320 + column * 8 + x;

						if ( bits >> ( 7 - x ) & 1 )
							*target = static_cast < uint8_t > ( attribute & 0x80 ? *target ^ ( attribute & 0x7F ) : attribute );
						else if ( !( attribute & 0x80 ) )
							*target = 0;

					}

				}

			}

			if ( teletype )
				column++;

		}

		if ( !teletype )
			return;

		if ( column >= columns ) {

			column = 0;
			row++;

		}

		if ( row >= rows ) {

			uint8_t fill = 0x07;

			if ( text ) {

				uint32_t base = ( mode == 7 ? 0xB0000 : 0xB8000 ) + page * static_cast < uint32_t > ( peek16 ( 0x44C ) );

				fill = peek8 ( base + ( ( rows - 1 ) * columns + column ) * 2 + 1 );

			} else
				fill = 0;

			scroll ( true, 1, fill, 0, 0, static_cast < uint8_t > ( rows - 1 ), static_cast < uint8_t > ( columns - 1 ) );
			row = rows - 1;

		}

		move_cursor ( page, static_cast < uint8_t > ( row ), static_cast < uint8_t > ( column ) );

	}

	void bios::print ( const char* text, uint8_t attribute ) {

		for ( int32_t i = 0; text [ i ]; i++ )
			put_character ( static_cast < uint8_t > ( text [ i ] ), attribute, peek8 ( 0x462 ), true, text [ i ] >= 32 );

	}

	uint32_t bios::video ( bios_frame& frame ) {

		vga& screen = *link.video;
		uint8_t page = frame.bh ( ) & 7;

		switch ( frame.ah ( ) ) {

			case 0x00:
				set_mode ( frame.al ( ), true );
				frame.set_al ( static_cast < uint8_t > ( frame.al ( ) > 7 && frame.al ( ) != 0x13 ? 0x20 : 0x30 ) );
				return 0;

			case 0x01: {

				uint8_t start = frame.ch ( ) & 0x3F;
				uint8_t end = frame.cl ( ) & 0x1F;
				int32_t height = screen.char_height;

				poke16 ( 0x460, static_cast < uint16_t > ( frame.ecx ) );

				if ( start <= 7 && end <= 7 && height > 8 && !( start & 0x20 ) ) {

					start = static_cast < uint8_t > ( start * height / 8 );
					end = static_cast < uint8_t > ( end * height / 8 + height / 8 - 1 );

				}

				screen.crtc [ 0x0A ] = start;
				screen.crtc [ 0x0B ] = end;
				return 0;

			}

			case 0x02:
				move_cursor ( page, frame.dh ( ), frame.dl ( ) );
				return 0;

			case 0x03: {

				uint16_t position = peek16 ( 0x450 + page * 2u );

				frame.set_dx ( position );
				frame.set_cx ( peek16 ( 0x460 ) );
				frame.set_ax ( 0 );
				return 0;

			}

			case 0x04:
				frame.set_ah ( 0 );
				return 0;

			case 0x05: {

				uint8_t active = frame.al ( ) & 7;
				uint32_t start = active * static_cast < uint32_t > ( peek16 ( 0x44C ) );
				uint16_t position = peek16 ( 0x450 + active * 2u );

				poke8 ( 0x462, active );
				poke16 ( 0x44E, static_cast < uint16_t > ( start ) );
				screen.crtc [ 0x0C ] = static_cast < uint8_t > ( start / 2 >> 8 );
				screen.crtc [ 0x0D ] = static_cast < uint8_t > ( start / 2 );
				move_cursor ( active, static_cast < uint8_t > ( position >> 8 ), static_cast < uint8_t > ( position ) );
				return 0;

			}

			case 0x06:
			case 0x07:
				scroll ( frame.ah ( ) == 0x06, frame.al ( ), frame.bh ( ), frame.ch ( ), frame.cl ( ), frame.dh ( ), frame.dl ( ) );
				return 0;

			case 0x08: {

				uint8_t mode = peek8 ( 0x449 );
				uint16_t position = peek16 ( 0x450 + page * 2u );
				uint32_t base = ( mode == 7 ? 0xB0000 : 0xB8000 ) + page * static_cast < uint32_t > ( peek16 ( 0x44C ) );
				uint32_t at = base + ( ( position >> 8 ) * static_cast < uint32_t > ( peek16 ( 0x44A ) ) + ( position & 0xFF ) ) * 2;

				frame.set_ax ( peek16 ( at ) );
				return 0;

			}

			case 0x09:
			case 0x0A: {

				uint16_t saved = peek16 ( 0x450 + page * 2u );
				uint32_t columns = peek16 ( 0x44A );
				uint32_t count = frame.ecx & 0xFFFF;
				uint32_t row = saved >> 8;
				uint32_t column = saved & 0xFF;

				for ( uint32_t i = 0; i < count && row <= peek8 ( 0x484 ); i++ ) {

					poke16 ( 0x450 + page * 2u, static_cast < uint16_t > ( row << 8 | column ) );
					put_character ( frame.al ( ), frame.bl ( ), page, false, frame.ah ( ) == 0x09 || peek8 ( 0x449 ) > 7 );

					if ( ++column >= columns ) {

						column = 0;
						row++;

					}

				}

				poke16 ( 0x450 + page * 2u, saved );
				return 0;

			}

			case 0x0B:

				if ( frame.bh ( ) == 0 )
					screen.attributes [ 0 ] = frame.bl ( ) & 0x0F;
				else {

					bool alternate = frame.bl ( ) & 1;

					screen.attributes [ 1 ] = alternate ? 0x3B : 0x3A;
					screen.attributes [ 2 ] = alternate ? 0x3D : 0x3C;
					screen.attributes [ 3 ] = alternate ? 0x3F : 0x3E;

				}

				return 0;

			case 0x0C:
			case 0x0D: {

				uint8_t mode = peek8 ( 0x449 );
				uint32_t x = frame.ecx & 0xFFFF;
				uint32_t y = frame.edx & 0xFFFF;

				if ( screen.kind == VGA_PLANAR ) {

					uint32_t limit_x = mode == 0x0D ? 320u : 640u;
					uint32_t limit_y = mode == 0x11 || mode == 0x12 ? 480u : ( mode == 0x10 ? 350u : 200u );

					if ( x < limit_x && y < limit_y ) {

						if ( frame.ah ( ) == 0x0D )
							frame.set_al ( screen.get_pixel ( x, y ) );
						else
							screen.put_pixel ( x, y, static_cast < uint8_t > ( frame.al ( ) & 0x0F ), ( frame.al ( ) & 0x80 ) != 0 );

					}

				} else if ( mode == 0x13 && x < 320 && y < 200 ) {

					uint8_t* pixel = memory ( 0xA0000 + y * 320 + x, 1 );

					if ( pixel ) {

						if ( frame.ah ( ) == 0x0D )
							frame.set_al ( *pixel );
						else
							*pixel = static_cast < uint8_t > ( frame.al ( ) & 0x80 ? *pixel ^ ( frame.al ( ) & 0x7F ) : frame.al ( ) );

					}

				} else if ( ( mode == 4 || mode == 5 || mode == 6 ) && y < 200 ) {

					bool fine = mode == 6;
					uint32_t at = 0xB8000 + ( y & 1 ) * 0x2000 + ( y >> 1 ) * 80 + ( fine ? x >> 3 : x >> 2 );
					uint32_t shift = fine ? 7 - ( x & 7 ) : ( 3 - ( x & 3 ) ) * 2;
					uint8_t mask = static_cast < uint8_t > ( ( fine ? 1 : 3 ) << shift );
					uint8_t value = peek8 ( at );

					if ( frame.ah ( ) == 0x0D )
						frame.set_al ( static_cast < uint8_t > ( ( value & mask ) >> shift ) );
					else {

						uint8_t bits = static_cast < uint8_t > ( ( frame.al ( ) & ( fine ? 1 : 3 ) ) << shift );

						poke8 ( at, static_cast < uint8_t > ( frame.al ( ) & 0x80 ? value ^ bits : ( value & ~mask ) | bits ) );

					}

				}

				return 0;

			}

			case 0x0E:
				put_character ( frame.al ( ), frame.bl ( ), peek8 ( 0x462 ), true, peek8 ( 0x449 ) > 7 );
				return 0;

			case 0x0F:
				frame.set_al ( static_cast < uint8_t > ( peek8 ( 0x449 ) | ( peek8 ( 0x487 ) & 0x80 ) ) );
				frame.set_ah ( static_cast < uint8_t > ( peek16 ( 0x44A ) ) );
				frame.set_bh ( peek8 ( 0x462 ) );
				return 0;

			case 0x10: {

				switch ( frame.al ( ) ) {

					case 0x00:
						screen.attributes [ frame.bl ( ) & 0x1F ] = frame.bh ( );
						break;

					case 0x01:
						screen.attributes [ 0x11 ] = frame.bh ( );
						break;

					case 0x02:

						if ( !need ( frame.es, static_cast < uint16_t > ( frame.edx ), 17 ) )
							return status_needs_input;

						for ( uint32_t i = 0; i < 17; i++ )
							screen.attributes [ i < 16 ? i : 0x11 ] = taken ( i );

						break;

					case 0x03:
						screen.attributes [ 0x10 ] = static_cast < uint8_t > ( frame.bl ( ) ? screen.attributes [ 0x10 ] | 0x08 : screen.attributes [ 0x10 ] & ~0x08 );
						break;

					case 0x07:
						frame.set_bh ( screen.attributes [ frame.bl ( ) & 0x1F ] );
						break;

					case 0x08:
						frame.set_bh ( screen.attributes [ 0x11 ] );
						break;

					case 0x09: {

						uint8_t table [ 17 ];

						for ( uint32_t i = 0; i < 17; i++ )
							table [ i ] = screen.attributes [ i < 16 ? i : 0x11 ];

						give ( frame.es, static_cast < uint16_t > ( frame.edx ), table, 17 );
						break;

					}

					case 0x10: {

						uint8_t index = static_cast < uint8_t > ( frame.ebx );

						screen.dac [ index ] [ 0 ] = frame.dh ( ) & 0x3F;
						screen.dac [ index ] [ 1 ] = frame.ch ( ) & 0x3F;
						screen.dac [ index ] [ 2 ] = frame.cl ( ) & 0x3F;
						break;

					}

					case 0x12: {

						uint32_t first = frame.ebx & 0xFF;
						uint32_t count = base::min ( frame.ecx & 0xFFFF, 256 - first );

						if ( !need ( frame.es, static_cast < uint16_t > ( frame.edx ), count * 3 ) )
							return status_needs_input;

						for ( uint32_t i = 0; i < count; i++ ) {

							for ( uint32_t c = 0; c < 3; c++ )
								screen.dac [ first + i ] [ c ] = taken ( i * 3 + c ) & 0x3F;

						}

						break;

					}

					case 0x15: {

						uint8_t index = static_cast < uint8_t > ( frame.ebx );

						frame.set_dh ( screen.dac [ index ] [ 0 ] );
						frame.set_ch ( screen.dac [ index ] [ 1 ] );
						frame.set_cl ( screen.dac [ index ] [ 2 ] );
						break;

					}

					case 0x17: {

						uint32_t first = frame.ebx & 0xFF;
						uint32_t count = base::min ( frame.ecx & 0xFFFF, 256 - first );
						uint8_t table [ 768 ];

						for ( uint32_t i = 0; i < count; i++ ) {

							for ( uint32_t c = 0; c < 3; c++ )
								table [ i * 3 + c ] = screen.dac [ first + i ] [ c ];

						}

						give ( frame.es, static_cast < uint16_t > ( frame.edx ), table, count * 3 );
						break;

					}

					case 0x18:
						screen.pel_mask = frame.bl ( );
						break;

					case 0x19:
						frame.set_bl ( screen.pel_mask );
						break;

					case 0x1A:
						frame.set_bx ( 0 );
						break;

					default:
						break;

				}

				return 0;

			}

			case 0x11: {

				uint8_t function = frame.al ( );

				if ( function == 0x30 ) {

					static const uint16_t places [ 8 ] = { 0, 0, font_14, font_8, font_8 + 0x400, font_14, font_16, font_16 };
					uint8_t which = frame.bh ( ) & 7;

					if ( which == 0 ) {

						frame.es = peek16 ( 0x1F * 4 + 2 );
						frame.ebp = ( frame.ebp & ~0xFFFFu ) | peek16 ( 0x1F * 4 );

					} else if ( which == 1 ) {

						frame.es = peek16 ( 0x43 * 4 + 2 );
						frame.ebp = ( frame.ebp & ~0xFFFFu ) | peek16 ( 0x43 * 4 );

					} else {

						frame.es = 0xC000;
						frame.ebp = ( frame.ebp & ~0xFFFFu ) | places [ which ];

					}

					frame.set_cx ( static_cast < uint16_t > ( screen.char_height ) );
					frame.set_dl ( peek8 ( 0x484 ) );
					return 0;

				}

				if ( function == 0x01 || function == 0x11 || function == 0x02 || function == 0x12 || function == 0x04 || function == 0x14 ) {

					int32_t height = function == 0x01 || function == 0x11 ? 14 : ( function == 0x02 || function == 0x12 ? 8 : 16 );

					screen.set_font ( height );

					if ( function & 0x10 ) {

						screen.rows = 400 / height;
						poke8 ( 0x484, static_cast < uint8_t > ( screen.rows - 1 ) );
						poke16 ( 0x485, static_cast < uint16_t > ( height ) );

					}

					return 0;

				}

				if ( function == 0x00 || function == 0x10 ) {

					uint32_t first = frame.edx & 0xFF;
					uint32_t count = base::min ( frame.ecx & 0xFFFF, 256 - first );
					uint32_t height = frame.bh ( ) ? base::min ( static_cast < uint32_t > ( frame.bh ( ) ), 32u ) : 16;

					if ( !need ( frame.es, static_cast < uint16_t > ( frame.ebp ), count * height ) )
						return status_needs_input;

					for ( uint32_t c = 0; c < count; c++ ) {

						for ( uint32_t line = 0; line < 32; line++ )
							screen.font [ first + c ] [ line ] = line < height ? taken ( c * height + line ) : 0;

					}

					screen.char_height = static_cast < int32_t > ( height > 32 ? 32 : height );

					if ( function == 0x10 ) {

						screen.rows = 400 / screen.char_height;
						poke8 ( 0x484, static_cast < uint8_t > ( screen.rows - 1 ) );
						poke16 ( 0x485, static_cast < uint16_t > ( screen.char_height ) );

					}

					return 0;

				}

				return 0;

			}

			case 0x12:

				if ( frame.bl ( ) == 0x10 ) {

					frame.set_bx ( 0x0003 );
					frame.set_cx ( 0x0009 );

				} else if ( frame.bl ( ) >= 0x30 && frame.bl ( ) <= 0x36 )
					frame.set_al ( 0x12 );

				return 0;

			case 0x13: {

				bool attributes = ( frame.al ( ) & 2 ) != 0;
				uint32_t count = base::min ( frame.ecx & 0xFFFF, attributes ? 0x7FFFu : 0xFFFFu );
				uint16_t saved = peek16 ( 0x450 + page * 2u );

				if ( !need ( frame.es, static_cast < uint16_t > ( frame.ebp ), attributes ? count * 2 : count ) )
					return status_needs_input;

				move_cursor ( page, frame.dh ( ), frame.dl ( ) );

				for ( uint32_t i = 0; i < count; i++ ) {

					uint8_t character = taken ( attributes ? i * 2 : i );
					uint8_t attribute = attributes ? taken ( i * 2 + 1 ) : frame.bl ( );

					put_character ( character, attribute, page, true, character >= 32 );

				}

				if ( !( frame.al ( ) & 1 ) )
					move_cursor ( page, static_cast < uint8_t > ( saved >> 8 ), static_cast < uint8_t > ( saved ) );

				return 0;

			}

			case 0x1A:

				if ( frame.al ( ) == 0 )
					frame.set_bx ( 0x0008 );

				frame.set_al ( 0x1A );
				return 0;

			case 0x1B: {

				uint8_t table [ 64 ] = { };
				uint16_t columns = peek16 ( 0x44A );
				uint16_t height = peek16 ( 0x485 );

				table [ 3 ] = 0xF0;
				table [ 4 ] = peek8 ( 0x449 );
				table [ 5 ] = static_cast < uint8_t > ( columns );
				table [ 6 ] = static_cast < uint8_t > ( columns >> 8 );
				table [ 0x22 ] = static_cast < uint8_t > ( peek8 ( 0x484 ) + 1 );
				table [ 0x23 ] = static_cast < uint8_t > ( height );
				table [ 0x24 ] = static_cast < uint8_t > ( height >> 8 );
				table [ 0x25 ] = 0x08;
				give ( frame.es, static_cast < uint16_t > ( frame.edi ), table, 64 );
				frame.set_al ( 0x1B );
				return 0;

			}

			case 0x4F:
				return vbe ( frame );

			default:
				return 0;

		}

	}

	uint32_t bios::vbe ( bios_frame& frame ) {

		vga& screen = *link.video;
		uint16_t segment = frame.es;
		uint16_t offset = static_cast < uint16_t > ( frame.edi );

		switch ( frame.al ( ) ) {

			case 0x00: {

				if ( !need ( segment, offset, 4 ) )
					return status_needs_input;

				bool wide = taken ( 0 ) == 'V' && taken ( 1 ) == 'B' && taken ( 2 ) == 'E' && taken ( 3 ) == '2';
				uint32_t size = wide ? 512 : 256;
				uint8_t info [ 512 ] = { };

				auto put16 = [ & ] ( uint32_t at, uint32_t value ) {

					info [ at ] = static_cast < uint8_t > ( value );
					info [ at + 1 ] = static_cast < uint8_t > ( value >> 8 );

				};

				auto pointer = [ & ] ( uint32_t at, uint32_t inside, uint32_t rom ) {

					if ( wide ) {

						put16 ( at, ( offset + inside ) & 0xFFFF );
						put16 ( at + 2, segment );

					} else {

						put16 ( at, rom );
						put16 ( at + 2, 0xC000 );

					}

				};

				info [ 0 ] = 'V';
				info [ 1 ] = 'E';
				info [ 2 ] = 'S';
				info [ 3 ] = 'A';
				put16 ( 4, 0x0300 );
				put16 ( 10, 0x0001 );
				put16 ( 18, static_cast < uint32_t > ( vga::vram_size >> 16 ) );
				put16 ( 20, 0x0100 );
				pointer ( 14, 34, mode_list );
				pointer ( 6, 256, text_oem );
				pointer ( 22, 288, text_vendor );
				pointer ( 26, 320, text_product );
				pointer ( 30, 352, text_revision );

				if ( wide ) {

					uint32_t at = 34;

					for ( int32_t i = 0; i < vga::mode_count; i++ ) {

						put16 ( at, vga::modes [ i ].number );
						at += 2;

					}

					put16 ( at, 0xFFFF );

					const char* strings [ 4 ] = { "Coldboot VGA", "Coldboot", "Coldboot Display", "1.0" };

					for ( int32_t s = 0; s < 4; s++ ) {

						for ( int32_t i = 0; strings [ s ] [ i ]; i++ )
							info [ 256 + s * 32 + i ] = static_cast < uint8_t > ( strings [ s ] [ i ] );

					}

				}

				give ( segment, offset, info, size );
				frame.set_ax ( 0x004F );
				return 0;

			}

			case 0x01: {

				const vbe_mode* mode = vga::find_mode ( static_cast < uint16_t > ( frame.ecx & 0x1FF ) );

				if ( !mode ) {

					frame.set_ax ( 0x014F );

					return 0;

				}

				uint8_t info [ 256 ] = { };
				uint32_t bytes = ( mode->bits + 7u ) / 8u;
				uint32_t pitch = mode->width * bytes;
				uint32_t pages = static_cast < uint32_t > ( vga::vram_size / ( static_cast < uint64_t > ( pitch ) * mode->height ) );
				uint8_t image_pages = static_cast < uint8_t > ( pages > 256 ? 255 : pages - 1 );
				uint8_t masks [ 8 ] = { };

				if ( mode->bits == 32 || mode->bits == 24 ) {

					static const uint8_t full [ 8 ] = { 8, 16, 8, 8, 8, 0, 8, 24 };

					for ( int32_t i = 0; i < 8; i++ )
						masks [ i ] = full [ i ];

					if ( mode->bits == 24 )
						masks [ 6 ] = masks [ 7 ] = 0;

				} else if ( mode->bits == 16 ) {

					static const uint8_t high [ 8 ] = { 5, 11, 6, 5, 5, 0, 0, 0 };

					for ( int32_t i = 0; i < 8; i++ )
						masks [ i ] = high [ i ];

				} else if ( mode->bits == 15 ) {

					static const uint8_t low [ 8 ] = { 5, 10, 5, 5, 5, 0, 1, 15 };

					for ( int32_t i = 0; i < 8; i++ )
						masks [ i ] = low [ i ];

				}

				auto put16 = [ & ] ( uint32_t at, uint32_t value ) {

					info [ at ] = static_cast < uint8_t > ( value );
					info [ at + 1 ] = static_cast < uint8_t > ( value >> 8 );

				};

				put16 ( 0x00, 0x00FB );
				put16 ( 0x10, pitch );
				put16 ( 0x12, mode->width );
				put16 ( 0x14, mode->height );
				info [ 0x16 ] = 8;
				info [ 0x17 ] = 16;
				info [ 0x18 ] = 1;
				info [ 0x19 ] = mode->bits;
				info [ 0x1A ] = 1;
				info [ 0x1B ] = static_cast < uint8_t > ( mode->bits == 8 ? 4 : 6 );
				info [ 0x1D ] = image_pages;
				info [ 0x1E ] = 1;

				for ( int32_t i = 0; i < 8; i++ ) {

					info [ 0x1F + i ] = masks [ i ];
					info [ 0x36 + i ] = masks [ i ];

				}

				uint32_t lfb = screen.lfb_address ( );

				put16 ( 0x28, lfb & 0xFFFF );
				put16 ( 0x2A, lfb >> 16 );
				put16 ( 0x32, pitch );
				info [ 0x34 ] = image_pages;
				info [ 0x35 ] = image_pages;
				give ( segment, offset, info, 256 );
				frame.set_ax ( 0x004F );
				return 0;

			}

			case 0x02: {

				uint16_t number = static_cast < uint16_t > ( frame.ebx & 0xFFFF );
				uint16_t mode = number & 0x1FF;

				if ( mode < 0x100 ) {

					set_mode ( static_cast < uint8_t > ( mode | ( number & 0x8000 ? 0x80 : 0 ) ), true );
					frame.set_ax ( 0x004F );

					return 0;

				}

				frame.set_ax ( static_cast < uint16_t > ( screen.set_vbe ( static_cast < uint16_t > ( mode | ( number & 0x8000 ) ) ) ? 0x004F : 0x014F ) );
				return 0;

			}

			case 0x03:
				frame.set_bx ( static_cast < uint16_t > ( screen.kind == VGA_LFB ? screen.vbe_number | 0x4000 : peek8 ( 0x449 ) ) );
				frame.set_ax ( 0x004F );
				return 0;

			case 0x04:

				if ( frame.dl ( ) == 0 )
					frame.set_bx ( 1 );

				frame.set_ax ( 0x004F );
				return 0;

			case 0x06: {

				if ( screen.kind != VGA_LFB ) {

					frame.set_ax ( 0x034F );

					return 0;

				}

				uint32_t bytes = ( screen.vbe_bits + 7u ) / 8u;

				if ( frame.bl ( ) == 0x00 )
					screen.vbe_pitch = ( frame.ecx & 0xFFFF ) * bytes;
				else if ( frame.bl ( ) == 0x02 )
					screen.vbe_pitch = frame.ecx & 0xFFFF;

				if ( screen.vbe_pitch < screen.vbe_width * bytes )
					screen.vbe_pitch = screen.vbe_width * bytes;

				uint32_t pitch = frame.bl ( ) == 0x03 ? 0x8000 : screen.vbe_pitch;

				frame.set_bx ( static_cast < uint16_t > ( pitch ) );
				frame.set_cx ( static_cast < uint16_t > ( pitch / bytes ) );
				frame.set_dx ( static_cast < uint16_t > ( base::min ( static_cast < uint64_t > ( vga::vram_size / pitch ), static_cast < uint64_t > ( 0xFFFF ) ) ) );
				frame.set_ax ( 0x004F );
				return 0;

			}

			case 0x07: {

				if ( screen.kind != VGA_LFB ) {

					frame.set_ax ( 0x034F );

					return 0;

				}

				uint32_t bytes = ( screen.vbe_bits + 7u ) / 8u;

				if ( frame.bl ( ) == 0x01 ) {

					uint32_t line = screen.vbe_start / screen.vbe_pitch;

					frame.set_cx ( static_cast < uint16_t > ( screen.vbe_start % screen.vbe_pitch / bytes ) );
					frame.set_dx ( static_cast < uint16_t > ( line ) );

				} else
					screen.vbe_start = ( frame.edx & 0xFFFF ) * screen.vbe_pitch + ( frame.ecx & 0xFFFF ) * bytes;

				frame.set_ax ( 0x004F );
				return 0;

			}

			case 0x08:

				if ( frame.bl ( ) == 0x00 )
					screen.dac_bits = frame.bh ( ) >= 8 ? 8 : 6;

				frame.set_bh ( screen.dac_bits );
				frame.set_ax ( 0x004F );
				return 0;

			case 0x09: {

				uint32_t first = base::min ( frame.edx & 0xFFFF, 256u );
				uint32_t count = base::min ( frame.ecx & 0xFFFF, 256 - first );

				if ( frame.bl ( ) == 0x01 ) {

					uint8_t table [ 1024 ];

					for ( uint32_t i = 0; i < count; i++ ) {

						table [ i * 4 ] = screen.dac [ first + i ] [ 2 ];
						table [ i * 4 + 1 ] = screen.dac [ first + i ] [ 1 ];
						table [ i * 4 + 2 ] = screen.dac [ first + i ] [ 0 ];
						table [ i * 4 + 3 ] = 0;

					}

					give ( segment, offset, table, count * 4 );

				} else {

					if ( !need ( segment, offset, count * 4 ) )
						return status_needs_input;

					for ( uint32_t i = 0; i < count; i++ ) {

						screen.dac [ first + i ] [ 2 ] = taken ( i * 4 );
						screen.dac [ first + i ] [ 1 ] = taken ( i * 4 + 1 );
						screen.dac [ first + i ] [ 0 ] = taken ( i * 4 + 2 );

					}

				}

				frame.set_ax ( 0x004F );
				return 0;

			}

			case 0x15: {

				if ( frame.bl ( ) == 0x00 ) {

					frame.set_bx ( 0x0002 );
					frame.set_ax ( 0x004F );

					return 0;

				}

				if ( frame.bl ( ) != 0x01 || ( frame.edx & 0xFFFF ) != 0 ) {

					frame.set_ax ( 0x014F );

					return 0;

				}

				uint8_t block [ 128 ];

				screen.edid ( block );
				give ( segment, offset, block, 128 );
				frame.set_ax ( 0x004F );
				return 0;

			}

			default:
				frame.set_ax ( 0x014F );
				return 0;

		}

	}

	bool bios::transfer ( uint8_t drive, uint64_t lba, uint32_t count, uint32_t address, bool writing, uint32_t& done ) {

		disk_file* file = drive < 0x80 || drive == optical_drive ? link.optical : link.hard_disk;
		uint32_t size = drive == optical_drive ? 2048 : 512;
		uint64_t offset = drive < 0x80 ? static_cast < uint64_t > ( emulated_lba ) * 2048 : 0;
		uint64_t total = 0;

		done = 0;

		if ( !file || !file->present ( ) )
			return false;

		if ( drive < 0x80 )
			total = emulated_media == 1 ? 2400 : ( emulated_media == 2 ? 2880 : 5760 );
		else
			total = file->size / size;

		if ( !count || lba >= total )
			return false;

		uint32_t allowed = static_cast < uint32_t > ( base::min ( static_cast < uint64_t > ( count ), total - lba ) );
		uint8_t* target = memory ( address, allowed * size );

		if ( !target )
			return false;

		bool ok = false;

		if ( writing )
			ok = drive >= 0x80 && drive != optical_drive && file->writable && file->write ( offset + lba * size, target, allowed * size );
		else
			ok = file->read ( offset + lba * size, target, allowed * size );

		if ( ok )
			done = allowed;

		return ok && allowed == count;

	}

	uint32_t bios::disk ( bios_frame& frame ) {

		uint8_t drive = frame.dl ( );
		uint8_t function = frame.ah ( );
		bool floppy = drive < 0x80;
		bool optical = drive == optical_drive;
		bool present = false;

		if ( floppy )
			present = drive == 0 && emulating && emulated_media >= 1 && emulated_media <= 3 && link.optical && link.optical->present ( );
		else if ( optical )
			present = link.optical && link.optical->present ( );
		else
			present = drive == 0x80 && link.hard_disk && link.hard_disk->present ( );

		auto finish = [ & ] ( uint8_t code ) {

			frame.set_ah ( code );
			frame.carry ( code != 0 );
			last_disk_status = code;
			poke8 ( floppy ? 0x441 : 0x474, code );

			return 0u;

		};

		if ( function == 0x01 ) {

			frame.set_ah ( last_disk_status );
			frame.carry ( last_disk_status != 0 );

			return 0;

		}

		if ( !present ) {

			if ( function == 0x15 ) {

				frame.set_ah ( 0 );
				frame.carry ( false );

				return 0;

			}

			if ( function == 0x08 && !floppy ) {

				frame.set_dl ( peek8 ( 0x475 ) );

				return finish ( 0x07 );

			}

			return finish ( floppy ? 0x80 : 0x01 );

		}

		uint32_t sector_size = optical ? 2048 : 512;
		uint64_t total = 0;
		uint32_t heads = floppy ? 2 : 255;
		uint32_t per_track = floppy ? ( emulated_media == 1 ? 15 : ( emulated_media == 2 ? 18 : 36 ) ) : 63;

		if ( floppy )
			total = static_cast < uint64_t > ( 80 ) * heads * per_track;
		else
			total = ( optical ? link.optical->size : link.hard_disk->size ) / sector_size;

		uint32_t cylinders = floppy ? 80 : static_cast < uint32_t > ( base::max ( base::min ( total / ( heads * per_track ), static_cast < uint64_t > ( 1024 ) ), static_cast < uint64_t > ( 1 ) ) );

		switch ( function ) {

			case 0x00:
			case 0x0D:
			case 0x09:
			case 0x0C:
			case 0x10:
			case 0x11:
			case 0x14:
			case 0x16:
			case 0x44:
			case 0x45:
			case 0x46:
			case 0x47:
			case 0x49:
				return finish ( 0 );

			case 0x02:
			case 0x03:
			case 0x04: {

				if ( optical )
					return finish ( 0x01 );

				uint32_t count = frame.al ( );
				uint32_t cylinder = frame.ch ( ) | static_cast < uint32_t > ( frame.cl ( ) & 0xC0 ) << 2;
				uint32_t sector = frame.cl ( ) & 0x3F;
				uint32_t head = frame.dh ( );

				if ( !sector || sector > per_track || head >= heads || !count )
					return finish ( 0x04 );

				if ( function == 0x04 )
					return finish ( 0 );

				uint64_t lba = ( static_cast < uint64_t > ( cylinder ) * heads + head ) * per_track + sector - 1;
				uint32_t done = 0;
				bool ok = transfer ( drive, lba, count, frame.es * 16u + ( frame.ebx & 0xFFFF ), function == 0x03, done );

				frame.set_al ( static_cast < uint8_t > ( done ) );

				return finish ( ok ? 0 : ( function == 0x03 ? 0x03 : 0x04 ) );

			}

			case 0x08: {

				if ( optical )
					return finish ( 0x01 );

				uint32_t top = cylinders - 1;

				frame.set_ch ( static_cast < uint8_t > ( top ) );
				frame.set_cl ( static_cast < uint8_t > ( per_track | ( top >> 2 & 0xC0 ) ) );
				frame.set_dh ( static_cast < uint8_t > ( heads - 1 ) );
				frame.set_dl ( static_cast < uint8_t > ( floppy ? 1 : peek8 ( 0x475 ) ) );
				frame.set_al ( 0 );

				if ( floppy ) {

					frame.set_bl ( static_cast < uint8_t > ( emulated_media == 1 ? 2 : ( emulated_media == 2 ? 4 : 5 ) ) );
					frame.es = 0xF000;
					frame.edi = ( frame.edi & ~0xFFFFu ) | disk_table_offset;

				}

				return finish ( 0 );

			}

			case 0x15:
				frame.set_cx ( static_cast < uint16_t > ( total >> 16 ) );
				frame.set_dx ( static_cast < uint16_t > ( total ) );
				frame.set_ah ( static_cast < uint8_t > ( floppy ? 0x01 : 0x03 ) );
				frame.carry ( false );
				return 0;

			case 0x41:

				if ( ( frame.ebx & 0xFFFF ) != 0x55AA )
					return finish ( 0x01 );

				frame.set_bx ( 0xAA55 );
				frame.set_cx ( 0x0007 );
				frame.set_ah ( 0x30 );
				frame.carry ( false );
				return 0;

			case 0x42:
			case 0x43: {

				uint32_t packet = frame.ds * 16u + ( frame.esi & 0xFFFF );
				uint8_t size = peek8 ( packet );
				uint32_t count = peek16 ( packet + 2 );
				uint32_t offset = peek16 ( packet + 4 );
				uint32_t segment = peek16 ( packet + 6 );
				uint64_t lba = peek32 ( packet + 8 ) | static_cast < uint64_t > ( peek32 ( packet + 12 ) ) << 32;
				uint32_t address = segment * 16 + offset;

				if ( offset == 0xFFFF && segment == 0xFFFF && size >= 0x18 )
					address = peek32 ( packet + 0x10 );

				if ( function == 0x43 && optical )
					return finish ( 0x03 );

				uint32_t done = 0;
				bool ok = !count || transfer ( drive, lba, count, address, function == 0x43, done );

				poke16 ( packet + 2, static_cast < uint16_t > ( done ) );

				return finish ( ok ? 0 : 0x04 );

			}

			case 0x48: {

				uint32_t buffer = frame.ds * 16u + ( frame.esi & 0xFFFF );
				uint16_t size = peek16 ( buffer );

				if ( size < 0x1A )
					return finish ( 0x01 );

				poke16 ( buffer + 2, static_cast < uint16_t > ( optical ? 0x0074 : 0x0003 ) );
				poke32 ( buffer + 4, optical ? 0xFFFFFFFFu : cylinders );
				poke32 ( buffer + 8, optical ? 0xFFFFFFFFu : heads );
				poke32 ( buffer + 12, optical ? 0xFFFFFFFFu : per_track );
				poke32 ( buffer + 16, static_cast < uint32_t > ( total ) );
				poke32 ( buffer + 20, static_cast < uint32_t > ( total >> 32 ) );
				poke16 ( buffer + 24, static_cast < uint16_t > ( sector_size ) );

				if ( size >= 0x1E ) {

					poke32 ( buffer + 0x1A, 0xFFFFFFFFu );
					poke16 ( buffer, 0x1E );

				} else
					poke16 ( buffer, 0x1A );

				return finish ( 0 );

			}

			case 0x4B: {

				if ( !boot_load_segment || ( frame.al ( ) != 0x00 && frame.al ( ) != 0x01 ) )
					return finish ( 0x01 );

				uint32_t packet = frame.ds * 16u + ( frame.esi & 0xFFFF );

				poke8 ( packet, 0x13 );
				poke8 ( packet + 1, boot_media );
				poke8 ( packet + 2, static_cast < uint8_t > ( boot_media ? 0x00 : optical_drive ) );
				poke8 ( packet + 3, 0 );
				poke32 ( packet + 4, boot_image_lba );
				poke16 ( packet + 8, 0 );
				poke16 ( packet + 10, 0 );
				poke16 ( packet + 12, boot_load_segment );
				poke16 ( packet + 14, boot_sector_count );
				poke8 ( packet + 16, 79 );
				poke8 ( packet + 17, static_cast < uint8_t > ( boot_media == 1 ? 15 : ( boot_media == 2 ? 18 : 36 ) ) );
				poke8 ( packet + 18, 1 );

				if ( frame.al ( ) == 0x00 )
					emulating = false;

				return finish ( 0 );

			}

			default:
				return finish ( 0x01 );

		}

	}

	uint32_t bios::system ( bios_frame& frame ) {

		hv::machine& vm = *link.vm;
		uint64_t top = vm.core.low_size - firmware_room;
		uint8_t function = frame.ah ( );

		auto unsupported = [ & ] ( ) {

			frame.set_ah ( 0x86 );
			frame.carry ( true );

			return 0u;

		};

		auto success = [ & ] ( ) {

			frame.set_ah ( 0 );
			frame.carry ( false );

			return 0u;

		};

		switch ( function ) {

			case 0x24:

				if ( frame.al ( ) == 0x01 )
					return success ( );

				if ( frame.al ( ) == 0x02 ) {

					frame.set_al ( 1 );

					return success ( );

				}

				if ( frame.al ( ) == 0x03 ) {

					frame.set_bx ( 0x0003 );

					return success ( );

				}

				return unsupported ( );

			case 0x4F:
				frame.carry ( true );
				return 0;

			case 0x86: {

				uint32_t microseconds = static_cast < uint32_t > ( frame.ecx & 0xFFFF ) << 16 | ( frame.edx & 0xFFFF );

				Sleep ( base::min ( microseconds / 1000, 1000u ) );

				return success ( );

			}

			case 0x87: {

				uint32_t table = frame.es * 16u + ( frame.esi & 0xFFFF );
				uint32_t from = peek16 ( table + 0x12 ) | static_cast < uint32_t > ( peek8 ( table + 0x14 ) ) << 16 | static_cast < uint32_t > ( peek8 ( table + 0x17 ) ) << 24;
				uint32_t to = peek16 ( table + 0x1A ) | static_cast < uint32_t > ( peek8 ( table + 0x1C ) ) << 16 | static_cast < uint32_t > ( peek8 ( table + 0x1F ) ) << 24;
				uint32_t bytes = ( frame.ecx & 0xFFFF ) * 2;
				const uint8_t* source = memory ( from, bytes );
				uint8_t* target = memory ( to, bytes );

				if ( !source || !target )
					return unsupported ( );

				for ( uint32_t i = 0; i < bytes; i++ )
					target [ i ] = source [ i ];

				frame.zero ( true );

				return success ( );

			}

			case 0x88:
				frame.set_ax ( static_cast < uint16_t > ( base::min ( ( top - 0x100000 ) / 1024, static_cast < uint64_t > ( 0xFC00 ) ) ) );
				frame.carry ( false );
				return 0;

			case 0x90:
			case 0x91:
				return success ( );

			case 0xC0:
				frame.es = 0xF000;
				frame.set_bx ( static_cast < uint16_t > ( config_offset ) );
				return success ( );

			case 0xC1:
				frame.es = static_cast < uint16_t > ( ebda >> 4 );
				frame.carry ( false );
				return 0;

			case 0xC2: {

				keyboard_controller& keys = *link.keyboard;
				dual_pic& pics = *link.pics;

				switch ( frame.al ( ) ) {

					case 0x00:

						if ( frame.bh ( ) == 1 && !peek32 ( ebda + mouse_handler ) ) {

							frame.set_ah ( 0x05 );
							frame.carry ( true );

							return 0;

						}

						mouse_enabled = frame.bh ( ) == 1;
						mouse_count = 0;
						keys.streaming = mouse_enabled;
						keys.config = static_cast < uint8_t > ( mouse_enabled ? ( keys.config | 0x02 ) & ~0x20 : keys.config & ~0x02 );

						if ( mouse_enabled ) {

							pics.write ( 0xA1, 1, pics.read ( 0xA1, 1 ) & ~0x10u );
							pics.write ( 0x21, 1, pics.read ( 0x21, 1 ) & ~0x04u );

						}

						return success ( );

					case 0x01:
						mouse_enabled = false;
						mouse_count = 0;
						keys.streaming = false;
						frame.set_bx ( 0x00AA );
						return success ( );

					case 0x02:
					case 0x03:
					case 0x05:
						mouse_count = 0;
						return success ( );

					case 0x04:
						frame.set_bh ( keys.mouse_id );
						return success ( );

					case 0x06:

						if ( frame.bh ( ) == 0 ) {

							frame.set_bl ( static_cast < uint8_t > ( mouse_enabled ? 0x20 : 0 ) );
							frame.set_cl ( 2 );
							frame.set_dl ( 100 );

						}

						return success ( );

					case 0x07:
						poke16 ( ebda + mouse_handler, static_cast < uint16_t > ( frame.ebx ) );
						poke16 ( ebda + mouse_handler + 2, frame.es );
						return success ( );

					default:
						frame.set_ah ( 0x01 );
						frame.carry ( true );
						return 0;

				}

			}

			case 0xE8: {

				if ( frame.al ( ) == 0x01 ) {

					uint32_t below = static_cast < uint32_t > ( base::min ( ( top - 0x100000 ) / 1024, static_cast < uint64_t > ( 0x3C00 ) ) );
					uint32_t above = top > 0x1000000 ? static_cast < uint32_t > ( base::min ( ( top - 0x1000000 ) / 65536, static_cast < uint64_t > ( 0xFFFF ) ) ) : 0;

					frame.set_ax ( static_cast < uint16_t > ( below ) );
					frame.set_cx ( static_cast < uint16_t > ( below ) );
					frame.set_bx ( static_cast < uint16_t > ( above ) );
					frame.set_dx ( static_cast < uint16_t > ( above ) );
					frame.carry ( false );

					return 0;

				}

				if ( frame.al ( ) != 0x20 || frame.edx != 0x534D4150 )
					return unsupported ( );

				struct region {

					uint64_t base;
					uint64_t length;
					uint32_t type;

				};

				region map [ 9 ] = {

					{ 0x00000000, ebda, 1 },
					{ ebda, 0xA0000 - ebda, 2 },
					{ 0xE0000, 0x20000, 2 },
					{ 0x100000, top - 0x100000, 1 },
					{ top, firmware_room, 2 },
					{ 0xFEC00000, 0x1000, 2 },
					{ 0xFEE00000, 0x1000, 2 },
					{ 0xFFFC0000, 0x40000, 2 },
					{ hv::partition::high_start, vm.core.high_size ( ), 1 },

				};

				uint32_t count = vm.core.high_size ( ) ? 9 : 8;
				uint32_t index = frame.ebx;

				if ( index >= count )
					return unsupported ( );

				uint32_t target = frame.es * 16u + ( frame.edi & 0xFFFF );
				uint32_t size = frame.ecx >= 24 ? 24 : 20;

				poke32 ( target, static_cast < uint32_t > ( map [ index ].base ) );
				poke32 ( target + 4, static_cast < uint32_t > ( map [ index ].base >> 32 ) );
				poke32 ( target + 8, static_cast < uint32_t > ( map [ index ].length ) );
				poke32 ( target + 12, static_cast < uint32_t > ( map [ index ].length >> 32 ) );
				poke32 ( target + 16, map [ index ].type );

				if ( size == 24 )
					poke32 ( target + 20, 1 );

				frame.eax = 0x534D4150;
				frame.ecx = size;
				frame.ebx = index + 1 < count ? index + 1 : 0;
				frame.carry ( false );

				return 0;

			}

			case 0xEC:
				return success ( );

			default:
				return unsupported ( );

		}

	}

	bool bios::push_key ( uint16_t code ) {

		uint16_t head = peek16 ( 0x41A );
		uint16_t tail = peek16 ( 0x41C );
		uint16_t start = peek16 ( 0x480 );
		uint16_t end = peek16 ( 0x482 );
		uint16_t next = static_cast < uint16_t > ( tail + 2 >= end ? start : tail + 2 );

		if ( next == head )
			return false;

		poke16 ( 0x400 + tail, code );
		poke16 ( 0x41C, next );

		return true;

	}

	void bios::keyboard_interrupt ( ) {

		uint8_t code = static_cast < uint8_t > ( link.keyboard->read ( 0x60, 1 ) );
		uint8_t flags = peek8 ( 0x417 );
		uint8_t flags2 = peek8 ( 0x418 );
		uint8_t flags3 = peek8 ( 0x496 );

		link.pics->write ( 0x20, 1, 0x20 );

		if ( code == 0xE0 || code == 0xE1 ) {

			extended_prefix = code == 0xE0;
			poke8 ( 0x496, static_cast < uint8_t > ( flags3 | 0x02 ) );

			return;

		}

		bool extended = extended_prefix;
		bool release = ( code & 0x80 ) != 0;
		uint8_t make = code & 0x7F;

		extended_prefix = false;
		flags3 = static_cast < uint8_t > ( flags3 & ~0x02 );

		auto set_bit = [ ] ( uint8_t& value, uint8_t bit, bool on ) {

			value = static_cast < uint8_t > ( on ? value | bit : value & ~bit );

		};

		if ( ( make == 0x2A || make == 0x36 ) && !extended ) {

			set_bit ( flags, make == 0x2A ? 0x02 : 0x01, !release );

		} else if ( make == 0x1D ) {

			if ( extended )
				set_bit ( flags3, 0x04, !release );
			else
				set_bit ( flags2, 0x01, !release );

			set_bit ( flags, 0x04, ( flags2 & 0x01 ) || ( flags3 & 0x04 ) );

		} else if ( make == 0x38 ) {

			if ( extended )
				set_bit ( flags3, 0x08, !release );
			else
				set_bit ( flags2, 0x02, !release );

			set_bit ( flags, 0x08, ( flags2 & 0x02 ) || ( flags3 & 0x08 ) );

		} else if ( make == 0x3A || make == 0x45 || make == 0x46 ) {

			uint8_t toggle = make == 0x3A ? 0x40 : ( make == 0x45 ? 0x20 : 0x10 );

			if ( !release && !( flags2 & toggle ) )
				flags ^= toggle;

			set_bit ( flags2, toggle, !release );

		}

		poke8 ( 0x417, flags );
		poke8 ( 0x418, flags2 );
		poke8 ( 0x496, flags3 );

		if ( release || make == 0x2A || make == 0x36 || make == 0x1D || make == 0x38 || make == 0x3A || make == 0x45 || make == 0x46 || make >= 0x59 )
			return;

		bool shift = ( flags & 0x03 ) != 0;
		bool control = ( flags & 0x04 ) != 0;
		bool alt = ( flags & 0x08 ) != 0;

		if ( control && alt && make == 0x53 ) {

			poke16 ( 0x472, 0x1234 );
			link.bus->request_reset ( );

			return;

		}

		uint8_t ascii = 0;
		uint8_t scan = make;

		if ( extended ) {

			if ( make == 0x1C )
				push_key ( static_cast < uint16_t > ( alt ? 0xA600 : ( control ? 0xE00A : 0xE00D ) ) );
			else if ( make == 0x35 )
				push_key ( static_cast < uint16_t > ( alt ? 0xA400 : ( control ? 0x9500 : 0xE02F ) ) );
			else if ( make >= 0x47 && make <= 0x53 && make != 0x4A && make != 0x4C && make != 0x4E ) {

				static const uint8_t controlled [ 13 ] = { 0x77, 0x8D, 0x84, 0, 0x73, 0, 0x74, 0, 0x75, 0x91, 0x76, 0x92, 0x93 };

				if ( alt )
					push_key ( static_cast < uint16_t > ( ( make + 0x50 ) << 8 ) );
				else if ( control )
					push_key ( static_cast < uint16_t > ( controlled [ make - 0x47 ] << 8 | 0xE0 ) );
				else
					push_key ( static_cast < uint16_t > ( make << 8 | 0xE0 ) );

			}

			return;

		}

		if ( make >= 0x3B && make <= 0x44 ) {

			scan = static_cast < uint8_t > ( alt ? make + 0x2D : ( control ? make + 0x23 : ( shift ? make + 0x19 : make ) ) );
			push_key ( static_cast < uint16_t > ( scan << 8 ) );

			return;

		}

		if ( make == 0x57 || make == 0x58 ) {

			scan = static_cast < uint8_t > ( ( alt ? 0x8B : ( control ? 0x89 : ( shift ? 0x87 : 0x85 ) ) ) + make - 0x57 );
			push_key ( static_cast < uint16_t > ( scan << 8 ) );

			return;

		}

		if ( make >= 0x47 && make <= 0x53 ) {

			static const uint8_t controlled [ 13 ] = { 0x77, 0x8D, 0x84, 0x8E, 0x73, 0x8F, 0x74, 0x90, 0x75, 0x91, 0x76, 0x92, 0x93 };

			if ( alt )
				return;

			if ( control ) {

				push_key ( static_cast < uint16_t > ( controlled [ make - 0x47 ] << 8 ) );

				return;

			}

			bool numeric = ( ( flags & 0x20 ) != 0 ) != shift;

			if ( make == 0x4A || make == 0x4E || numeric )
				ascii = static_cast < uint8_t > ( normal_keys [ make ] );

			push_key ( static_cast < uint16_t > ( scan << 8 | ascii ) );

			return;

		}

		char plain = normal_keys [ make ];
		bool letter = plain >= 'a' && plain <= 'z';

		if ( alt ) {

			if ( make >= 0x02 && make <= 0x0D )
				scan = static_cast < uint8_t > ( make + 0x76 );

			push_key ( static_cast < uint16_t > ( scan << 8 ) );

			return;

		}

		if ( control ) {

			if ( letter )
				ascii = static_cast < uint8_t > ( plain & 0x1F );
			else if ( make == 0x1A )
				ascii = 0x1B;
			else if ( make == 0x1B )
				ascii = 0x1D;
			else if ( make == 0x2B )
				ascii = 0x1C;
			else if ( make == 0x0C )
				ascii = 0x1F;
			else if ( make == 0x07 )
				ascii = 0x1E;
			else if ( make == 0x1C )
				ascii = 0x0A;
			else if ( make == 0x0E )
				ascii = 0x7F;
			else if ( make == 0x01 )
				ascii = 0x1B;
			else if ( make == 0x39 )
				ascii = ' ';
			else if ( make == 0x03 ) {

				push_key ( 0x0300 );

				return;

			} else if ( make == 0x0F ) {

				push_key ( 0x9400 );

				return;

			} else
				return;

			push_key ( static_cast < uint16_t > ( scan << 8 | ascii ) );

			return;

		}

		if ( letter )
			ascii = static_cast < uint8_t > ( shift != ( ( flags & 0x40 ) != 0 ) ? plain - 32 : plain );
		else
			ascii = static_cast < uint8_t > ( shift ? shifted_keys [ make ] : plain );

		if ( !ascii && make != 0x0F )
			return;

		push_key ( static_cast < uint16_t > ( scan << 8 | ascii ) );

	}

	uint32_t bios::keyboard_service ( bios_frame& frame ) {

		uint8_t function = frame.ah ( );
		uint16_t head = peek16 ( 0x41A );
		uint16_t tail = peek16 ( 0x41C );

		auto convert = [ ] ( uint16_t code, bool extended ) {

			if ( !extended && ( code & 0xFF ) == 0xE0 && ( code >> 8 ) )
				code = static_cast < uint16_t > ( code & 0xFF00 );

			return code;

		};

		switch ( function ) {

			case 0x00:
			case 0x10: {

				if ( head == tail )
					return 1;

				uint16_t code = peek16 ( 0x400 + head );
				uint16_t next = static_cast < uint16_t > ( head + 2 >= peek16 ( 0x482 ) ? peek16 ( 0x480 ) : head + 2 );

				poke16 ( 0x41A, next );
				frame.set_ax ( convert ( code, function == 0x10 ) );

				return 0;

			}

			case 0x01:
			case 0x11:

				if ( head == tail ) {

					frame.zero ( true );

					return 0;

				}

				frame.set_ax ( convert ( peek16 ( 0x400 + head ), function == 0x11 ) );
				frame.zero ( false );

				return 0;

			case 0x02:
				frame.set_al ( peek8 ( 0x417 ) );
				return 0;

			case 0x12: {

				uint8_t flags2 = peek8 ( 0x418 );
				uint8_t flags3 = peek8 ( 0x496 );
				uint8_t high = static_cast < uint8_t > ( ( flags2 & 0x73 ) | ( flags3 & 0x0C ) );

				frame.set_al ( peek8 ( 0x417 ) );
				frame.set_ah ( high );

				return 0;

			}

			case 0x05:
				frame.set_al ( static_cast < uint8_t > ( push_key ( static_cast < uint16_t > ( frame.ecx ) ) ? 0 : 1 ) );
				return 0;

			case 0x09:
				frame.set_al ( 0x30 );
				return 0;

			case 0x0A:
				frame.set_bx ( 0x41AB );
				return 0;

			default:
				return 0;

		}

	}

	uint32_t bios::time ( bios_frame& frame ) {

		real_time_clock& clock = *link.clock;

		auto cmos = [ & ] ( uint8_t index ) {

			clock.write ( 0x70, 1, index );

			return static_cast < uint8_t > ( clock.read ( 0x71, 1 ) );

		};

		auto store = [ & ] ( uint8_t index, uint8_t value ) {

			clock.write ( 0x70, 1, index );
			clock.write ( 0x71, 1, value );

		};

		switch ( frame.ah ( ) ) {

			case 0x00:
				frame.set_cx ( peek16 ( 0x46E ) );
				frame.set_dx ( peek16 ( 0x46C ) );
				frame.set_al ( peek8 ( 0x470 ) );
				poke8 ( 0x470, 0 );
				frame.carry ( false );
				return 0;

			case 0x01:
				poke16 ( 0x46E, static_cast < uint16_t > ( frame.ecx ) );
				poke16 ( 0x46C, static_cast < uint16_t > ( frame.edx ) );
				poke8 ( 0x470, 0 );
				frame.carry ( false );
				return 0;

			case 0x02:
				frame.set_ch ( cmos ( 4 ) );
				frame.set_cl ( cmos ( 2 ) );
				frame.set_dh ( cmos ( 0 ) );
				frame.set_dl ( 0 );
				frame.carry ( false );
				return 0;

			case 0x03:
				store ( 4, frame.ch ( ) );
				store ( 2, frame.cl ( ) );
				store ( 0, frame.dh ( ) );
				frame.carry ( false );
				return 0;

			case 0x04:
				frame.set_ch ( cmos ( 0x32 ) );
				frame.set_cl ( cmos ( 9 ) );
				frame.set_dh ( cmos ( 8 ) );
				frame.set_dl ( cmos ( 7 ) );
				frame.carry ( false );
				return 0;

			case 0x05:
				store ( 0x32, frame.ch ( ) );
				store ( 9, frame.cl ( ) );
				store ( 8, frame.dh ( ) );
				store ( 7, frame.dl ( ) );
				frame.carry ( false );
				return 0;

			case 0xB1:
				return pci_service ( frame );

			default:
				frame.set_ah ( 0x86 );
				frame.carry ( true );
				return 0;

		}

	}

	uint32_t bios::pci_service ( bios_frame& frame ) {

		pci_bus& bus = *link.pci;
		uint32_t saved = bus.address;

		auto config_read = [ & ] ( uint32_t function, uint32_t offset, uint32_t size ) {

			bus.write ( 0xCF8, 4, 0x80000000u | function << 8 | ( offset & 0xFC ) );

			return static_cast < uint32_t > ( bus.read ( 0xCFC + ( offset & 3 ), size ) );

		};

		auto config_write = [ & ] ( uint32_t function, uint32_t offset, uint32_t size, uint32_t value ) {

			bus.write ( 0xCF8, 4, 0x80000000u | function << 8 | ( offset & 0xFC ) );
			bus.write ( 0xCFC + ( offset & 3 ), size, value );

		};

		uint32_t result = 0;
		uint32_t function = frame.bl ( );
		uint32_t offset = frame.edi & 0xFF;

		frame.carry ( false );
		frame.set_ah ( 0 );

		switch ( frame.al ( ) ) {

			case 0x01:
				frame.edx = 0x20494350;
				frame.set_al ( 0x01 );
				frame.set_bx ( 0x0210 );
				frame.set_cl ( 0 );
				frame.edi = 0;
				break;

			case 0x02:
			case 0x03: {

				uint32_t index = frame.esi & 0xFFFF;
				bool found = false;

				for ( uint32_t slot = 0; slot < 256 && !found; slot++ ) {

					uint32_t identity = config_read ( slot, 0, 4 );

					if ( identity == 0xFFFFFFFFu )
						continue;

					bool match = frame.al ( ) == 0x02 ? ( identity & 0xFFFF ) == ( frame.edx & 0xFFFF ) && identity >> 16 == ( frame.ecx & 0xFFFF ) : config_read ( slot, 8, 4 ) >> 8 == ( frame.ecx & 0xFFFFFF );

					if ( match && index-- == 0 ) {

						frame.set_bh ( 0 );
						frame.set_bl ( static_cast < uint8_t > ( slot ) );
						found = true;

					}

				}

				if ( !found ) {

					frame.set_ah ( 0x86 );
					frame.carry ( true );

				}

				break;

			}

			case 0x08:
				result = config_read ( function, offset, 1 );
				frame.set_cl ( static_cast < uint8_t > ( result ) );
				break;

			case 0x09:
				result = config_read ( function, offset, 2 );
				frame.set_cx ( static_cast < uint16_t > ( result ) );
				break;

			case 0x0A:
				frame.ecx = config_read ( function, offset, 4 );
				break;

			case 0x0B:
				config_write ( function, offset, 1, frame.cl ( ) );
				break;

			case 0x0C:
				config_write ( function, offset, 2, frame.ecx & 0xFFFF );
				break;

			case 0x0D:
				config_write ( function, offset, 4, frame.ecx );
				break;

			default:
				frame.set_ah ( 0x81 );
				frame.carry ( true );
				break;

		}

		bus.address = saved;

		return 0;

	}

	uint32_t bios::mouse_interrupt ( ) {

		uint8_t value = static_cast < uint8_t > ( link.keyboard->read ( 0x60, 1 ) );

		link.pics->write ( 0xA0, 1, 0x20 );
		link.pics->write ( 0x20, 1, 0x20 );

		if ( !mouse_enabled || ( !mouse_count && !( value & 0x08 ) ) )
			return 0;

		mouse_bytes [ mouse_count++ ] = value;

		if ( mouse_count < 3 )
			return 0;

		mouse_count = 0;

		if ( !peek32 ( ebda + mouse_handler ) )
			return 0;

		poke16 ( ebda + mouse_packet, mouse_bytes [ 0 ] );
		poke16 ( ebda + mouse_packet + 2, mouse_bytes [ 1 ] );
		poke16 ( ebda + mouse_packet + 4, mouse_bytes [ 2 ] );
		poke16 ( ebda + mouse_packet + 6, 0 );

		return 2;

	}

	bool bios::boot_optical ( bios_frame& frame ) {

		disk_file* file = link.optical;
		uint8_t sector [ 2048 ];

		if ( !file || !file->present ( ) || !file->read ( 17 * 2048, sector, 2048 ) )
			return false;

		static const char identity [ ] = "EL TORITO SPECIFICATION";

		if ( sector [ 0 ] != 0 || sector [ 1 ] != 'C' || sector [ 2 ] != 'D' || sector [ 3 ] != '0' || sector [ 4 ] != '0' || sector [ 5 ] != '1' )
			return false;

		for ( int32_t i = 0; identity [ i ]; i++ ) {

			if ( sector [ 7 + i ] != static_cast < uint8_t > ( identity [ i ] ) )
				return false;

		}

		uint32_t catalog = static_cast < uint32_t > ( sector [ 0x47 ] ) | static_cast < uint32_t > ( sector [ 0x48 ] ) << 8 | static_cast < uint32_t > ( sector [ 0x49 ] ) << 16 | static_cast < uint32_t > ( sector [ 0x4A ] ) << 24;

		if ( !file->read ( static_cast < uint64_t > ( catalog ) * 2048, sector, 2048 ) || sector [ 0 ] != 0x01 || sector [ 0x1E ] != 0x55 || sector [ 0x1F ] != 0xAA || sector [ 0x20 ] != 0x88 )
			return false;

		uint8_t media = sector [ 0x21 ] & 0x0F;
		uint16_t segment = static_cast < uint16_t > ( sector [ 0x22 ] | sector [ 0x23 ] << 8 );
		uint16_t count = static_cast < uint16_t > ( sector [ 0x26 ] | sector [ 0x27 ] << 8 );
		uint32_t image = static_cast < uint32_t > ( sector [ 0x28 ] ) | static_cast < uint32_t > ( sector [ 0x29 ] ) << 8 | static_cast < uint32_t > ( sector [ 0x2A ] ) << 16 | static_cast < uint32_t > ( sector [ 0x2B ] ) << 24;

		if ( !segment )
			segment = 0x07C0;

		if ( !count )
			count = 4;

		boot_media = media;
		boot_image_lba = image;
		boot_load_segment = segment;
		boot_sector_count = count;

		if ( media == 0 ) {

			uint32_t bytes = count * 512u;
			uint8_t* target = memory ( segment * 16u, bytes );

			if ( !target || !file->read ( static_cast < uint64_t > ( image ) * 2048, target, bytes ) )
				return false;

			frame.cs = segment;
			frame.ip = 0;
			frame.edx = ( frame.edx & ~0xFFu ) | optical_drive;
			copy_text ( last_boot, "dvd", sizeof ( last_boot ) );

			return true;

		}

		if ( media < 1 || media > 3 )
			return false;

		uint8_t* target = memory ( 0x7C00, 512 );

		if ( !target || !file->read ( static_cast < uint64_t > ( image ) * 2048, target, 512 ) )
			return false;

		emulating = true;
		emulated_lba = image;
		emulated_media = media;
		poke16 ( 0x410, static_cast < uint16_t > ( peek16 ( 0x410 ) | 0x0001 ) );
		frame.cs = 0;
		frame.ip = 0x7C00;
		frame.edx = frame.edx & ~0xFFu;
		copy_text ( last_boot, "floppy image on the dvd", sizeof ( last_boot ) );

		return true;

	}

	bool bios::boot_disk ( bios_frame& frame ) {

		disk_file* file = link.hard_disk;
		uint8_t* target = memory ( 0x7C00, 512 );

		if ( !file || !file->present ( ) || !target || !file->read ( 0, target, 512 ) || target [ 510 ] != 0x55 || target [ 511 ] != 0xAA )
			return false;

		frame.cs = 0;
		frame.ip = 0x7C00;
		frame.edx = ( frame.edx & ~0xFFu ) | 0x80;
		copy_text ( last_boot, "hard disk", sizeof ( last_boot ) );

		return true;

	}

	uint32_t bios::boot ( bios_frame& frame ) {

		boot_attempts++;
		frame.flags = static_cast < uint16_t > ( ( frame.flags | 0x0200 ) & ~0x0400 );

		bool booted = link.optical_first ? ( boot_optical ( frame ) || boot_disk ( frame ) ) : ( boot_disk ( frame ) || boot_optical ( frame ) );

		if ( booted )
			return 0;

		print ( "No bootable medium. Insert a disc or attach a hard disk, then press any key.\r\n", 0x07 );
		frame.cs = 0xF000;
		frame.ip = static_cast < uint16_t > ( no_boot_offset );

		return 0;

	}

}