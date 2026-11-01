#include "video/cga.hh"
#include "machine/archive.hh"

namespace machine {

	void cga::transfer ( archive& io ) {

		io.field ( crtc );
		io.field ( selected );
		io.field ( mode );
		io.field ( color );
		io.field ( frames );
		io.field ( now );
		io.field ( last_frame );

		if ( !io.writing ( ) )
			render ( );

	}

	static const uint8_t default_crtc [ 16 ] = { 0x71, 0x50, 0x5A, 0x0A, 0x1F, 0x06, 0x19, 0x1C, 0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00 };

	cga::cga ( bus& memory ) {

		vram = memory.backing ( 0xB8000 );
		pixels = static_cast < uint32_t* > ( runtime_allocate_zero ( sizeof ( uint32_t ) * max_width * max_height ) );
		reset ( );

	}

	cga::~cga ( ) {

		runtime_release ( pixels );

	}

	void cga::reset ( ) {

		for ( int32_t i = 0; i < 16; i++ )
			crtc [ i ] = default_crtc [ i ];

		crtc [ 16 ] = 0;
		crtc [ 17 ] = 0;
		selected = 0;
		mode = 0;
		color = 0;
		frames = 0;
		last_frame = 0;

	}

	uint32_t cga::line_dots ( ) const {

		return ( static_cast < uint32_t > ( crtc [ 0 ] ) + 1 ) * character_dots ( );

	}

	uint32_t cga::frame_lines ( ) const {

		uint32_t lines = ( static_cast < uint32_t > ( crtc [ 4 ] & 0x7F ) + 1 ) * ( static_cast < uint32_t > ( crtc [ 9 ] & 0x1F ) + 1 ) + ( crtc [ 5 ] & 0x1F );

		return lines < 2 ? 2 : lines;

	}

	uint8_t cga::status ( uint64_t cycles ) const {

		uint64_t frame = static_cast < uint64_t > ( line_dots ( ) ) * frame_lines ( );
		uint64_t position = cycles * 3 % frame;
		uint32_t line = static_cast < uint32_t > ( position / line_dots ( ) );
		uint32_t column = static_cast < uint32_t > ( position % line_dots ( ) / character_dots ( ) );
		uint32_t row_lines = ( crtc [ 9 ] & 0x1F ) + 1u;
		uint32_t retrace = ( crtc [ 7 ] & 0x7F ) * row_lines;
		bool visible = line < ( crtc [ 6 ] & 0x7Fu ) * row_lines && column < crtc [ 1 ];
		bool vertical = line >= retrace && line < retrace + 16;

		return static_cast < uint8_t > ( 0xF0 | ( visible ? 0 : 0x01 ) | ( vertical ? 0x08 : 0 ) );

	}

	bool cga::update ( uint64_t cycles ) {

		now = cycles;

		uint64_t frame = static_cast < uint64_t > ( line_dots ( ) ) * frame_lines ( );
		uint64_t index = cycles * 3 / frame;

		if ( index < last_frame )
			last_frame = index;

		if ( index == last_frame )
			return false;

		last_frame = index;
		frames++;
		render ( );

		return true;

	}

	void cga::render ( ) {

		int32_t columns = crtc [ 1 ];
		int32_t rows = crtc [ 6 ] & 0x7F;
		int32_t row_lines = ( crtc [ 9 ] & 0x1F ) + 1;
		uint32_t start = static_cast < uint32_t > ( crtc [ 12 ] << 8 | crtc [ 13 ] );
		bool graphics = ( mode & 0x02 ) != 0;
		int32_t cell = graphics ? 16 : ( mode & 0x01 ? 8 : 16 );

		if ( columns * cell > max_width )
			columns = max_width / cell;

		if ( rows * row_lines > max_height )
			rows = max_height / row_lines;

		width = columns * cell > 0 ? columns * cell : 640;
		height = rows * row_lines > 0 ? rows * row_lines : 200;

		if ( !( mode & 0x08 ) || columns <= 0 || rows <= 0 ) {

			for ( int32_t i = 0; i < width * height; i++ )
				pixels [ i ] = 0;

			return;

		}

		if ( graphics ) {

			uint32_t colors [ 4 ];
			uint32_t bright = color & 0x10 ? 8 : 0;
			static const uint8_t sets [ 3 ] [ 3 ] = { { 2, 4, 6 }, { 3, 5, 7 }, { 3, 4, 7 } };
			const uint8_t* set = sets [ mode & 0x04 ? 2 : ( color & 0x20 ? 1 : 0 ) ];

			colors [ 0 ] = palette [ color & 0x0F ];

			for ( int32_t i = 0; i < 3; i++ )
				colors [ i + 1 ] = palette [ set [ i ] + bright ];

			for ( int32_t row = 0; row < rows; row++ ) {

				for ( int32_t scan = 0; scan < row_lines; scan++ ) {

					uint32_t* out = pixels + ( row * row_lines + scan ) * width;
					uint32_t base = ( start + static_cast < uint32_t > ( row * columns ) ) * 2 + ( scan & 1 ) * 0x2000;

					for ( int32_t i = 0; i < columns * 2; i++ ) {

						uint8_t byte = vram [ ( base + static_cast < uint32_t > ( i ) ) & 0x3FFF ];

						if ( mode & 0x10 ) {

							for ( int32_t bit = 7; bit >= 0; bit-- )
								*out++ = byte >> bit & 1 ? palette [ color & 0x0F ] : 0;

						} else {

							for ( int32_t shift = 6; shift >= 0; shift -= 2 ) {

								uint32_t value = colors [ byte >> shift & 3 ];

								*out++ = value;
								*out++ = value;

							}

						}

					}

				}

			}

			return;

		}

		uint32_t cursor = static_cast < uint32_t > ( crtc [ 14 ] << 8 | crtc [ 15 ] ) & 0x3FFF;
		int32_t cursor_first = crtc [ 10 ] & 0x1F;
		int32_t cursor_last = crtc [ 11 ] & 0x1F;
		bool cursor_on = ( crtc [ 10 ] & 0x60 ) != 0x20 && ( frames & 8 );
		bool blink_on = ( frames & 16 ) != 0;
		int32_t step = cell / 8;

		for ( int32_t row = 0; row < rows; row++ ) {

			for ( int32_t column = 0; column < columns; column++ ) {

				uint32_t address = ( start + static_cast < uint32_t > ( row * columns + column ) ) & 0x3FFF;
				uint8_t character = vram [ address * 2 & 0x3FFF ];
				uint8_t attribute = vram [ ( address * 2 + 1 ) & 0x3FFF ];
				uint32_t foreground = palette [ attribute & 0x0F ];
				uint32_t background = palette [ mode & 0x20 ? ( attribute >> 4 ) & 7 : attribute >> 4 ];
				bool hidden = ( mode & 0x20 ) && ( attribute & 0x80 ) && !blink_on;
				bool here = cursor_on && address == cursor;

				for ( int32_t scan = 0; scan < row_lines; scan++ ) {

					uint8_t bits = scan < 8 && !hidden ? font_8x8 [ character ] [ scan ] : 0;
					bool in_cursor = cursor_first <= cursor_last ? scan >= cursor_first && scan <= cursor_last : scan >= cursor_first || scan <= cursor_last;

					if ( here && in_cursor )
						bits = 0xFF;

					uint32_t* out = pixels + ( row * row_lines + scan ) * width + column * cell;

					for ( int32_t bit = 7; bit >= 0; bit-- ) {

						uint32_t value = bits >> bit & 1 ? foreground : background;

						for ( int32_t repeat = 0; repeat < step; repeat++ )
							*out++ = value;

					}

				}

			}

		}

	}

	uint8_t cga::in ( uint16_t port ) {

		switch ( port & 0x0F ) {

			case 0x01:
			case 0x03:
			case 0x05:
			case 0x07:
				return selected >= 14 && selected <= 17 ? crtc [ selected ] : 0;

			case 0x0A:
				return status ( clock ? *clock : now );

			default:
				return 0xFF;

		}

	}

	void cga::out ( uint16_t port, uint8_t value ) {

		switch ( port & 0x0F ) {

			case 0x00:
			case 0x02:
			case 0x04:
			case 0x06:
				selected = static_cast < uint8_t > ( value & 0x1F );
				return;

			case 0x01:
			case 0x03:
			case 0x05:
			case 0x07:

				if ( selected < 16 )
					crtc [ selected ] = value;

				return;

			case 0x08:
				mode = static_cast < uint8_t > ( value & 0x3F );
				return;

			case 0x09:
				color = static_cast < uint8_t > ( value & 0x3F );
				return;

			default:
				return;

		}

	}

}