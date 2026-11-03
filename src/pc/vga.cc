#include "pc/vga.hh"
#include "video/cga.hh"

namespace pc {

	const vbe_mode vga::modes [ ] = {

		{ 0x100, 640, 400, 8 },
		{ 0x101, 640, 480, 8 },
		{ 0x103, 800, 600, 8 },
		{ 0x105, 1024, 768, 8 },
		{ 0x110, 640, 480, 15 },
		{ 0x111, 640, 480, 16 },
		{ 0x112, 640, 480, 32 },
		{ 0x113, 800, 600, 15 },
		{ 0x114, 800, 600, 16 },
		{ 0x115, 800, 600, 32 },
		{ 0x116, 1024, 768, 15 },
		{ 0x117, 1024, 768, 16 },
		{ 0x118, 1024, 768, 32 },
		{ 0x11A, 1280, 1024, 16 },
		{ 0x11B, 1280, 1024, 32 },
		{ 0x140, 1280, 720, 32 },
		{ 0x141, 1280, 800, 32 },
		{ 0x142, 1366, 768, 32 },
		{ 0x143, 1440, 900, 32 },
		{ 0x144, 1600, 900, 32 },
		{ 0x145, 1600, 1200, 32 },
		{ 0x146, 1680, 1050, 32 },
		{ 0x147, 1920, 1080, 32 },
		{ 0x148, 1920, 1200, 32 },
		{ 0x149, 2560, 1440, 32 },
		{ 0x14A, 2560, 1600, 32 },
		{ 0x14B, 1280, 960, 32 },
		{ 0x14C, 1152, 864, 32 },
		{ 0x14D, 1360, 768, 32 },

	};

	const int32_t vga::mode_count = sizeof ( modes ) / sizeof ( modes [ 0 ] );

	static uint8_t expand ( uint8_t value, uint8_t bits ) {

		return bits == 8 ? value : static_cast < uint8_t > ( ( value & 0x3F ) << 2 | ( value & 0x3F ) >> 4 );

	}

	vga::vga ( hv::machine& target ) : vm ( target ) {

		vram = static_cast < uint8_t* > ( VirtualAlloc ( nullptr, vram_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );
		planes = static_cast < uint8_t* > ( runtime_allocate_zero ( 4 * 0x10000 ) );
		identify ( 0xC01D, 0x0001, 0x03, 0x00, 0x00, 0x01, 0x00 );
		set_bar ( 0, static_cast < uint32_t > ( vram_size ), false, default_lfb );
		config [ 0x10 ] |= 0x08;
		config [ 0x04 ] = 0x03;
		config [ 0x2C ] = 0x1D;
		config [ 0x2D ] = 0xC0;
		config [ 0x2E ] = 0x01;
		config [ 0x2F ] = 0x00;
		reset ( );

	}

	vga::~vga ( ) {

		if ( mapped_at )
			vm.core.unmap_window ( mapped_at );

		if ( vram )
			VirtualFree ( vram, 0, MEM_RELEASE );

		runtime_release ( planes );

	}

	uint32_t vga::planar_pitch ( ) const {

		uint32_t offset = crtc [ 0x13 ];

		return offset ? offset * 2 : ( legacy_mode == 0x0D ? 40u : 80u );

	}

	uint8_t vga::read_plane ( uint32_t offset ) {

		offset &= 0xFFFF;

		for ( int32_t p = 0; p < 4; p++ )
			latches [ p ] = planes [ p * 0x10000 + offset ];

		if ( !( graphics [ 5 ] & 0x08 ) )
			return latches [ graphics [ 4 ] & 3 ];

		uint8_t match = 0xFF;

		for ( int32_t p = 0; p < 4; p++ ) {

			if ( !( graphics [ 7 ] >> p & 1 ) )
				continue;

			uint8_t wanted = graphics [ 2 ] >> p & 1 ? 0xFF : 0x00;

			match &= static_cast < uint8_t > ( ~( latches [ p ] ^ wanted ) );

		}

		return match;

	}

	void vga::write_plane ( uint32_t offset, uint8_t value ) {

		offset &= 0xFFFF;

		uint8_t mode = graphics [ 5 ] & 3;
		uint8_t rotate = graphics [ 3 ] & 7;
		uint8_t function = graphics [ 3 ] >> 3 & 3;
		uint8_t mask = graphics [ 8 ];
		uint8_t rotated = static_cast < uint8_t > ( ( value >> rotate ) | ( value << ( 8 - rotate ) ) );

		if ( mode == 3 ) {

			mask &= rotated;

		}

		for ( int32_t p = 0; p < 4; p++ ) {

			if ( !( sequencer [ 2 ] >> p & 1 ) )
				continue;

			uint8_t latch = latches [ p ];
			uint8_t source = 0;

			if ( mode == 1 ) {

				planes [ p * 0x10000 + offset ] = latch;

				continue;

			}

			if ( mode == 0 )
				source = graphics [ 1 ] >> p & 1 ? ( graphics [ 0 ] >> p & 1 ? 0xFF : 0x00 ) : rotated;
			else if ( mode == 2 )
				source = value >> p & 1 ? 0xFF : 0x00;
			else
				source = graphics [ 0 ] >> p & 1 ? 0xFF : 0x00;

			if ( function == 1 )
				source &= latch;
			else if ( function == 2 )
				source |= latch;
			else if ( function == 3 )
				source ^= latch;

			planes [ p * 0x10000 + offset ] = static_cast < uint8_t > ( ( source & mask ) | ( latch & ~mask ) );

		}

	}

	void vga::put_pixel ( uint32_t x, uint32_t y, uint8_t value, bool flip ) {

		uint32_t offset = ( y * planar_pitch ( ) + x / 8 ) & 0xFFFF;
		uint8_t bit = static_cast < uint8_t > ( 0x80 >> ( x & 7 ) );

		for ( int32_t p = 0; p < 4; p++ ) {

			uint8_t& cell = planes [ p * 0x10000 + offset ];
			bool on = ( value >> p & 1 ) != 0;

			if ( flip ) {

				if ( on )
					cell ^= bit;

			} else
				cell = static_cast < uint8_t > ( on ? cell | bit : cell & ~bit );

		}

	}

	uint8_t vga::get_pixel ( uint32_t x, uint32_t y ) const {

		uint32_t offset = ( y * planar_pitch ( ) + x / 8 ) & 0xFFFF;
		uint8_t bit = static_cast < uint8_t > ( 0x80 >> ( x & 7 ) );
		uint8_t value = 0;

		for ( int32_t p = 0; p < 4; p++ ) {

			if ( planes [ p * 0x10000 + offset ] & bit )
				value = static_cast < uint8_t > ( value | 1 << p );

		}

		return value;

	}

	void vga::load_dac ( bool wide ) {

		if ( wide ) {

			default_palette ( );

			return;

		}

		for ( int32_t i = 0; i < 256; i++ ) {

			for ( int32_t c = 0; c < 3; c++ ) {

				int32_t primary = 2 - c;
				int32_t secondary = 5 - c;
				uint8_t value = 0;

				if ( i < 64 )
					value = static_cast < uint8_t > ( ( i >> primary & 1 ? 0x2A : 0 ) + ( i >> secondary & 1 ? 0x15 : 0 ) );

				dac [ i ] [ c ] = value;

			}

		}

	}

	void vga::default_palette ( ) {

		static const uint8_t ega [ 16 ] [ 3 ] = {

			{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x2A }, { 0x00, 0x2A, 0x00 }, { 0x00, 0x2A, 0x2A },
			{ 0x2A, 0x00, 0x00 }, { 0x2A, 0x00, 0x2A }, { 0x2A, 0x15, 0x00 }, { 0x2A, 0x2A, 0x2A },
			{ 0x15, 0x15, 0x15 }, { 0x15, 0x15, 0x3F }, { 0x15, 0x3F, 0x15 }, { 0x15, 0x3F, 0x3F },
			{ 0x3F, 0x15, 0x15 }, { 0x3F, 0x15, 0x3F }, { 0x3F, 0x3F, 0x15 }, { 0x3F, 0x3F, 0x3F },

		};

		static const uint8_t grays [ 16 ] = { 0x00, 0x05, 0x08, 0x0B, 0x0E, 0x11, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2D, 0x32, 0x38, 0x3F };
		static const uint8_t levels [ 9 ] [ 5 ] = {

			{ 0x00, 0x10, 0x1F, 0x2F, 0x3F }, { 0x1F, 0x27, 0x2F, 0x37, 0x3F }, { 0x2D, 0x31, 0x36, 0x3A, 0x3F },
			{ 0x00, 0x07, 0x0E, 0x15, 0x1C }, { 0x0E, 0x11, 0x15, 0x18, 0x1C }, { 0x14, 0x16, 0x18, 0x1A, 0x1C },
			{ 0x00, 0x04, 0x08, 0x0C, 0x10 }, { 0x08, 0x0A, 0x0C, 0x0E, 0x10 }, { 0x0B, 0x0C, 0x0D, 0x0F, 0x10 },

		};

		static const uint8_t hues [ 24 ] [ 3 ] = {

			{ 0, 0, 4 }, { 1, 0, 4 }, { 2, 0, 4 }, { 3, 0, 4 }, { 4, 0, 4 }, { 4, 0, 3 }, { 4, 0, 2 }, { 4, 0, 1 },
			{ 4, 0, 0 }, { 4, 1, 0 }, { 4, 2, 0 }, { 4, 3, 0 }, { 4, 4, 0 }, { 3, 4, 0 }, { 2, 4, 0 }, { 1, 4, 0 },
			{ 0, 4, 0 }, { 0, 4, 1 }, { 0, 4, 2 }, { 0, 4, 3 }, { 0, 4, 4 }, { 0, 3, 4 }, { 0, 2, 4 }, { 0, 1, 4 },

		};

		for ( int32_t i = 0; i < 16; i++ ) {

			for ( int32_t c = 0; c < 3; c++ )
				dac [ i ] [ c ] = ega [ i ] [ c ];

			for ( int32_t c = 0; c < 3; c++ )
				dac [ 16 + i ] [ c ] = grays [ i ];

		}

		for ( int32_t group = 0; group < 9; group++ ) {

			for ( int32_t hue = 0; hue < 24; hue++ ) {

				for ( int32_t c = 0; c < 3; c++ )
					dac [ 32 + group * 24 + hue ] [ c ] = levels [ group ] [ hues [ hue ] [ c ] ];

			}

		}

		for ( int32_t i = 248; i < 256; i++ )
			dac [ i ] [ 0 ] = dac [ i ] [ 1 ] = dac [ i ] [ 2 ] = 0;

		static const uint8_t standard [ 16 ] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F };

		for ( int32_t i = 0; i < 16; i++ )
			attributes [ i ] = standard [ i ];

		attributes [ 0x10 ] = 0x0C;
		attributes [ 0x11 ] = 0x00;
		attributes [ 0x12 ] = 0x0F;
		attributes [ 0x13 ] = 0x08;
		attributes [ 0x14 ] = 0x00;
		dac_bits = 6;

	}

	void vga::reset ( ) {

		for ( uint8_t& value : crtc )
			value = 0;

		for ( uint8_t& value : sequencer )
			value = 0;

		for ( uint8_t& value : graphics )
			value = 0;

		default_palette ( );
		set_font ( 16 );
		misc = 0x67;
		pel_mask = 0xFF;
		attribute_flip = false;
		dac_part = 0;
		set_legacy ( 3 );

	}

	void vga::map ( ) {

		uint32_t wanted = ( config [ 0x04 ] & 0x02 ) ? bar_address ( 0 ) : 0;

		if ( wanted && ( wanted < 0x80000000u || wanted > 0xFE000000u ) )
			wanted = 0;

		if ( wanted == mapped_at )
			return;

		if ( mapped_at )
			vm.core.unmap_window ( mapped_at );

		mapped_at = 0;

		if ( wanted && vm.core.map_window ( vram, wanted, vram_size ) )
			mapped_at = wanted;

	}

	void vga::written ( uint32_t offset ) {

		if ( offset <= 0x05 || ( offset >= 0x10 && offset <= 0x13 ) )
			map ( );

	}

	void vga::set_font ( int32_t height ) {

		char_height = height == 8 || height == 14 ? height : 16;

		for ( int32_t c = 0; c < 256; c++ ) {

			for ( int32_t line = 0; line < 32; line++ )
				font [ c ] [ line ] = 0;

			for ( int32_t line = 0; line < char_height; line++ ) {

				int32_t source = char_height == 8 ? line : ( char_height == 14 ? line * 8 / 14 : line / 2 );

				font [ c ] [ line ] = machine::font_8x8 [ c ] [ source ];

			}

		}

	}

	void vga::set_legacy ( uint8_t mode ) {

		legacy_mode = static_cast < uint8_t > ( mode & 0x7F );
		vbe_number = 0;
		vbe_start = 0;
		crtc [ 0x0C ] = 0;
		crtc [ 0x0D ] = 0;
		crtc [ 0x0A ] = 0x0E;
		crtc [ 0x0B ] = 0x0F;
		attributes [ 0x10 ] = 0x0C;

		static const uint8_t standard [ 16 ] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F };

		for ( int32_t i = 0; i < 16; i++ )
			attributes [ i ] = standard [ i ];

		load_dac ( legacy_mode == 0x13 );
		dac_bits = 6;

		switch ( legacy_mode ) {

			case 0x00:
			case 0x01:
				kind = VGA_TEXT;
				columns = 40;
				rows = 25;
				break;

			case 0x04:
			case 0x05:
				kind = VGA_CGA;
				attributes [ 1 ] = 0x3B;
				attributes [ 2 ] = 0x3D;
				attributes [ 3 ] = 0x3F;
				break;

			case 0x06:
				kind = VGA_CGA;
				attributes [ 1 ] = 0x3F;
				break;

			case 0x0D:
			case 0x0E:
			case 0x0F:
			case 0x10:
			case 0x11:
			case 0x12:
				kind = VGA_PLANAR;
				attributes [ 0x10 ] = 0x01;
				break;

			case 0x13:
				kind = VGA_LINEAR;
				attributes [ 0x10 ] = 0x41;
				break;

			default:
				kind = VGA_TEXT;
				columns = 80;
				rows = 25;
				break;

		}

		if ( kind == VGA_TEXT )
			rows = 400 / char_height;

		if ( kind == VGA_PLANAR ) {

			sequencer [ 2 ] = 0x0F;
			sequencer [ 4 ] = 0x06;

			for ( uint8_t& value : graphics )
				value = 0;

			graphics [ 5 ] = 0x00;
			graphics [ 6 ] = 0x05;
			graphics [ 7 ] = 0x0F;
			graphics [ 8 ] = 0xFF;
			crtc [ 0x13 ] = static_cast < uint8_t > ( legacy_mode == 0x0D ? 20 : 40 );

			if ( legacy_mode == 0x11 )
				attributes [ 1 ] = 0x3F;

			vm.core.trap ( 0xA0000, 0x10000 );

		} else
			vm.core.untrap ( );

	}

	const vbe_mode* vga::find_mode ( uint16_t number ) {

		for ( int32_t i = 0; i < mode_count; i++ ) {

			if ( modes [ i ].number == ( number & 0x1FF ) )
				return &modes [ i ];

		}

		return nullptr;

	}

	bool vga::set_vbe ( uint16_t number ) {

		const vbe_mode* mode = find_mode ( number );

		if ( !mode )
			return false;

		uint32_t bytes = ( mode->bits + 7u ) / 8u;

		vm.core.untrap ( );

		if ( mode->bits == 8 )
			load_dac ( true );

		kind = VGA_LFB;
		vbe_number = mode->number;
		vbe_width = mode->width;
		vbe_height = mode->height;
		vbe_bits = mode->bits;
		vbe_pitch = mode->width * bytes;
		vbe_start = 0;
		legacy_mode = 0x7F;

		if ( !( number & 0x8000 ) ) {

			uint64_t used = static_cast < uint64_t > ( vbe_pitch ) * vbe_height;

			for ( uint64_t i = 0; i < used && i < vram_size; i++ )
				vram [ i ] = 0;

		}

		return true;

	}

	uint32_t vga::color ( uint8_t index ) const {

		return static_cast < uint32_t > ( expand ( dac [ index ] [ 0 ], dac_bits ) ) << 16 | static_cast < uint32_t > ( expand ( dac [ index ] [ 1 ], dac_bits ) ) << 8 | expand ( dac [ index ] [ 2 ], dac_bits );

	}

	uint64_t vga::read ( uint64_t port, uint32_t size ) {

		uint64_t value = 0;
		bool memory = port >= 0xA0000;

		for ( uint32_t i = 0; i < size && i < 8; i++ ) {

			uint8_t part = memory ? read_plane ( static_cast < uint32_t > ( port - 0xA0000 + i ) ) : read_port ( static_cast < uint32_t > ( port + i ) );

			value |= static_cast < uint64_t > ( part ) << ( i * 8 );

		}

		return value;

	}

	void vga::write ( uint64_t port, uint32_t size, uint64_t value ) {

		bool memory = port >= 0xA0000;

		for ( uint32_t i = 0; i < size && i < 8; i++ ) {

			uint8_t part = static_cast < uint8_t > ( value >> ( i * 8 ) );

			if ( memory )
				write_plane ( static_cast < uint32_t > ( port - 0xA0000 + i ), part );
			else
				write_port ( static_cast < uint32_t > ( port + i ), part );

		}

	}

	uint8_t vga::read_port ( uint32_t port ) {

		switch ( port ) {

			case 0x3C0:
				return attribute_index;

			case 0x3C1:
				return attributes [ attribute_index & 0x1F ];

			case 0x3C2:
				return 0x10;

			case 0x3C4:
				return sequencer_index;

			case 0x3C5:
				return sequencer [ sequencer_index & 7 ];

			case 0x3C6:
				return pel_mask;

			case 0x3C7:
				return dac_state;

			case 0x3C8:
				return dac_write;

			case 0x3C9: {

				uint8_t value = dac [ dac_read ] [ dac_part ];

				if ( ++dac_part == 3 ) {

					dac_part = 0;
					dac_read++;

				}

				return value;

			}

			case 0x3CA:
				return 0;

			case 0x3CC:
				return misc;

			case 0x3CE:
				return graphics_index;

			case 0x3CF:
				return graphics [ graphics_index & 15 ];

			case 0x3B4:
			case 0x3D4:
				return crtc_index;

			case 0x3B5:
			case 0x3D5:
				return crtc [ crtc_index & 31 ];

			case 0x3BA:
			case 0x3DA: {

				uint64_t phase = host_nanoseconds ( ) % 16666667ull;
				uint8_t value = phase < 1250000ull ? 0x09 : ( phase % 31746ull < 6000ull ? 0x01 : 0x00 );

				attribute_flip = false;

				return value;

			}

			default:
				return 0xFF;

		}

	}

	void vga::write_port ( uint32_t port, uint8_t value ) {

		switch ( port ) {

			case 0x3C0:

				if ( !attribute_flip )
					attribute_index = value;
				else if ( ( attribute_index & 0x1F ) < 0x15 )
					attributes [ attribute_index & 0x1F ] = value;

				attribute_flip = !attribute_flip;
				break;

			case 0x3C2:
				misc = value;
				break;

			case 0x3C4:
				sequencer_index = value;
				break;

			case 0x3C5:
				sequencer [ sequencer_index & 7 ] = value;
				break;

			case 0x3C6:
				pel_mask = value;
				break;

			case 0x3C7:
				dac_read = value;
				dac_part = 0;
				dac_state = 0x00;
				break;

			case 0x3C8:
				dac_write = value;
				dac_part = 0;
				dac_state = 0x03;
				break;

			case 0x3C9:
				dac [ dac_write ] [ dac_part ] = static_cast < uint8_t > ( dac_bits == 8 ? value : value & 0x3F );

				if ( ++dac_part == 3 ) {

					dac_part = 0;
					dac_write++;

				}

				break;

			case 0x3CE:
				graphics_index = value;
				break;

			case 0x3CF:
				graphics [ graphics_index & 15 ] = value;
				break;

			case 0x3B4:
			case 0x3D4:
				crtc_index = value;
				break;

			case 0x3B5:
			case 0x3D5:
				crtc [ crtc_index & 31 ] = value;
				break;

			default:
				break;

		}

	}

	void vga::build_edid ( uint32_t across, uint32_t down, uint8_t* out ) {

		static const uint8_t base_block [ 128 ] = {

			0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x0C, 0x56, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
			0x2C, 0x24, 0x01, 0x03, 0x80, 0x34, 0x1D, 0x78, 0x0A, 0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26,
			0x0F, 0x50, 0x54, 0x21, 0x08, 0x00, 0x81, 0x80, 0x81, 0xC0, 0x81, 0x00, 0x95, 0x00, 0xA9, 0xC0,
			0xB3, 0x00, 0xD1, 0xC0, 0xD1, 0x00, 0x64, 0x19, 0x00, 0x40, 0x41, 0x00, 0x26, 0x30, 0x18, 0x88,
			0x36, 0x00, 0x08, 0x22, 0x21, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x43, 0x6F, 0x6C,
			0x64, 0x62, 0x6F, 0x6F, 0x74, 0x20, 0x56, 0x4D, 0x0A, 0x20, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x32,
			0x4B, 0x1E, 0x5A, 0x14, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

		};

		for ( int32_t i = 0; i < 128; i++ )
			out [ i ] = base_block [ i ];

		if ( across != 1024 || down != 768 ) {

			uint32_t blank_across = 160;
			uint32_t blank_down = base::max ( down / 25u, 23u );
			uint32_t clock = ( ( across + blank_across ) * ( down + blank_down ) * 60u + 5000u ) / 10000u;
			uint32_t wide_mm = 520;
			uint32_t tall_mm = wide_mm * down / across;
			uint8_t* timing = out + 54;

			out [ 12 ] = static_cast < uint8_t > ( across );
			out [ 13 ] = static_cast < uint8_t > ( across >> 8 );
			out [ 14 ] = static_cast < uint8_t > ( down );
			out [ 15 ] = static_cast < uint8_t > ( down >> 8 );
			timing [ 0 ] = static_cast < uint8_t > ( clock );
			timing [ 1 ] = static_cast < uint8_t > ( clock >> 8 );
			timing [ 2 ] = static_cast < uint8_t > ( across );
			timing [ 3 ] = static_cast < uint8_t > ( blank_across );
			timing [ 4 ] = static_cast < uint8_t > ( ( across >> 8 ) << 4 | blank_across >> 8 );
			timing [ 5 ] = static_cast < uint8_t > ( down );
			timing [ 6 ] = static_cast < uint8_t > ( blank_down );
			timing [ 7 ] = static_cast < uint8_t > ( ( down >> 8 ) << 4 | blank_down >> 8 );
			timing [ 8 ] = 48;
			timing [ 9 ] = 32;
			timing [ 10 ] = 3 << 4 | 6;
			timing [ 11 ] = 0;
			timing [ 12 ] = static_cast < uint8_t > ( wide_mm );
			timing [ 13 ] = static_cast < uint8_t > ( tall_mm );
			timing [ 14 ] = static_cast < uint8_t > ( ( wide_mm >> 8 ) << 4 | tall_mm >> 8 );
			timing [ 15 ] = 0;
			timing [ 16 ] = 0;
			timing [ 17 ] = 0x18;

		}

		uint8_t sum = 0;

		for ( int32_t i = 0; i < 127; i++ )
			sum = static_cast < uint8_t > ( sum + out [ i ] );

		out [ 127 ] = static_cast < uint8_t > ( 0x100 - sum );

	}

	bool vga::render ( uint32_t* out, int32_t& width, int32_t& height, double now ) {

		uint32_t difference = 0;
		bool drawn = paint ( out, width, height, now, difference );

		if ( drawn && ( difference || width != drawn_width || height != drawn_height ) )
			changes++;

		if ( drawn ) {

			drawn_width = width;
			drawn_height = height;

		}

		return drawn;

	}

	bool vga::paint ( uint32_t* out, int32_t& width, int32_t& height, double now, uint32_t& difference ) {

		if ( !vm.core.memory || vm.core.low_size < 0xC0000 )
			return false;

		const uint8_t* low = vm.core.memory + 0xA0000;

		difference = kind == VGA_LFB ? 0u : 1u;

		if ( kind == VGA_LFB ) {

			uint32_t bytes = ( vbe_bits + 7u ) / 8u;

			width = vbe_width;
			height = vbe_height;

			for ( int32_t y = 0; y < height; y++ ) {

				uint64_t row = vbe_start + static_cast < uint64_t > ( y ) * vbe_pitch;
				uint32_t* target = out + y * width;

				if ( row + static_cast < uint64_t > ( width ) * bytes > vram_size ) {

					for ( int32_t x = 0; x < width; x++ ) {

						difference |= target [ x ];
						target [ x ] = 0;

					}

					continue;

				}

				const uint8_t* source = vram + row;

				if ( vbe_bits == 32 ) {

					const uint32_t* pixels = reinterpret_cast < const uint32_t* > ( source );

					for ( int32_t x = 0; x < width; x++ ) {

						uint32_t value = pixels [ x ] & 0xFFFFFF;

						difference |= target [ x ] ^ value;
						target [ x ] = value;

					}

				} else if ( vbe_bits == 24 ) {

					for ( int32_t x = 0; x < width; x++ ) {

						uint32_t value = static_cast < uint32_t > ( source [ x * 3 + 2 ] ) << 16 | static_cast < uint32_t > ( source [ x * 3 + 1 ] ) << 8 | source [ x * 3 ];

						difference |= target [ x ] ^ value;
						target [ x ] = value;

					}

				} else if ( vbe_bits == 16 || vbe_bits == 15 ) {

					for ( int32_t x = 0; x < width; x++ ) {

						uint32_t value = static_cast < uint32_t > ( source [ x * 2 ] ) | static_cast < uint32_t > ( source [ x * 2 + 1 ] ) << 8;
						uint32_t red = vbe_bits == 16 ? value >> 11 & 31 : value >> 10 & 31;
						uint32_t green = vbe_bits == 16 ? ( value >> 5 & 63 ) : ( value >> 5 & 31 ) << 1;
						uint32_t blue = value & 31;
						uint32_t pixel = ( red << 3 | red >> 2 ) << 16 | ( green << 2 | green >> 4 ) << 8 | ( blue << 3 | blue >> 2 );

						difference |= target [ x ] ^ pixel;
						target [ x ] = pixel;

					}

				} else {

					for ( int32_t x = 0; x < width; x++ ) {

						uint32_t value = color ( source [ x ] );

						difference |= target [ x ] ^ value;
						target [ x ] = value;

					}

				}

			}

			return true;

		}

		if ( kind == VGA_LINEAR ) {

			width = 320;
			height = 200;

			for ( int32_t i = 0; i < 320 * 200; i++ )
				out [ i ] = color ( low [ i ] );

			return true;

		}

		if ( kind == VGA_CGA ) {

			const uint8_t* screen = low + 0x18000;
			bool fine = legacy_mode == 6;

			width = fine ? 640 : 320;
			height = 200;

			for ( int32_t y = 0; y < 200; y++ ) {

				const uint8_t* row = screen + ( y & 1 ) * 0x2000 + ( y >> 1 ) * 80;

				for ( int32_t x = 0; x < width; x++ ) {

					uint8_t value = fine ? ( row [ x >> 3 ] >> ( 7 - ( x & 7 ) ) & 1 ) : ( row [ x >> 2 ] >> ( ( 3 - ( x & 3 ) ) * 2 ) & 3 );

					out [ y * width + x ] = color ( static_cast < uint8_t > ( ( attributes [ 0x14 ] & 0x0C ) << 4 | attributes [ value ] ) );

				}

			}

			return true;

		}

		if ( kind == VGA_PLANAR ) {

			width = legacy_mode == 0x0D ? 320 : 640;
			height = legacy_mode == 0x12 || legacy_mode == 0x11 ? 480 : ( legacy_mode == 0x10 ? 350 : 200 );

			uint32_t pitch = planar_pitch ( );
			uint32_t start = static_cast < uint32_t > ( crtc [ 0x0C ] ) << 8 | crtc [ 0x0D ];
			uint8_t enable = attributes [ 0x12 ] & 0x0F;
			uint32_t palette [ 16 ];

			for ( int32_t i = 0; i < 16; i++ )
				palette [ i ] = color ( static_cast < uint8_t > ( ( attributes [ 0x14 ] & 0x0C ) << 4 | ( attributes [ i ] & 0x3F ) ) );

			for ( int32_t y = 0; y < height; y++ ) {

				uint32_t row = start + static_cast < uint32_t > ( y ) * pitch;
				uint32_t* target = out + y * width;

				for ( int32_t x = 0; x < width; x += 8 ) {

					uint32_t offset = ( row + static_cast < uint32_t > ( x ) / 8 ) & 0xFFFF;
					uint8_t bits [ 4 ] = { planes [ offset ], planes [ 0x10000 + offset ], planes [ 0x20000 + offset ], planes [ 0x30000 + offset ] };

					for ( int32_t b = 0; b < 8 && x + b < width; b++ ) {

						uint8_t index = 0;

						for ( int32_t p = 0; p < 4; p++ )
							index = static_cast < uint8_t > ( index | ( bits [ p ] >> ( 7 - b ) & 1 ) << p );

						target [ x + b ] = palette [ index & enable ];

					}

				}

			}

			return true;

		}

		const uint8_t* text = low + ( legacy_mode == 7 ? 0x10000 : 0x18000 );
		uint32_t start = ( static_cast < uint32_t > ( crtc [ 0x0C ] ) << 8 | crtc [ 0x0D ] ) * 2;
		uint32_t cursor = ( static_cast < uint32_t > ( crtc [ 0x0E ] ) << 8 | crtc [ 0x0F ] ) * 2;
		bool blink_mode = ( attributes [ 0x10 ] & 0x08 ) != 0;
		bool blink_on = static_cast < int64_t > ( now * 1000.0 ) % 534 < 267;
		bool cursor_on = static_cast < int64_t > ( now * 1000.0 ) % 266 < 133;
		int32_t cursor_top = crtc [ 0x0A ] & 0x1F;
		int32_t cursor_bottom = crtc [ 0x0B ] & 0x1F;
		bool cursor_shown = !( crtc [ 0x0A ] & 0x20 ) && cursor_top <= cursor_bottom;
		int32_t cell = 9;
		uint32_t palette [ 16 ];

		for ( int32_t i = 0; i < 16; i++ )
			palette [ i ] = color ( static_cast < uint8_t > ( ( attributes [ 0x14 ] & 0x0C ) << 4 | ( attributes [ i ] & 0x3F ) ) );

		width = columns * cell;
		height = rows * char_height;

		if ( width > max_width || height > max_height )
			return false;

		for ( int32_t row = 0; row < rows; row++ ) {

			for ( int32_t column = 0; column < columns; column++ ) {

				uint32_t at = ( start + static_cast < uint32_t > ( row * columns + column ) * 2 ) & 0x7FFF;
				uint8_t character = text [ at ];
				uint8_t attribute = text [ at + 1 ];
				uint32_t foreground = palette [ attribute & 0x0F ];
				uint32_t background = palette [ blink_mode ? attribute >> 4 & 7 : attribute >> 4 ];
				bool hidden = blink_mode && ( attribute & 0x80 ) && !blink_on;
				bool here = cursor_shown && cursor_on && at == ( cursor & 0x7FFF ) && ( start <= cursor );
				bool graphic = character >= 0xC0 && character <= 0xDF && ( attributes [ 0x10 ] & 0x04 );

				for ( int32_t line = 0; line < char_height; line++ ) {

					uint8_t bits = hidden ? 0 : font [ character ] [ line ];

					if ( here && line >= cursor_top && line <= cursor_bottom )
						bits = 0xFF;

					uint32_t* target = out + ( row * char_height + line ) * width + column * cell;

					for ( int32_t x = 0; x < 8; x++ )
						target [ x ] = bits >> ( 7 - x ) & 1 ? foreground : background;

					target [ 8 ] = ( graphic || ( here && line >= cursor_top && line <= cursor_bottom ) ) && ( bits & 1 ) ? foreground : background;

				}

			}

		}

		return true;

	}

}