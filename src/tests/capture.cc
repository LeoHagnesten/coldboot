#ifdef COLDBOOT_DEBUG

#include "base/files.hh"
#include "base/png.hh"
#include "machine/xt.hh"
#include "tests/harness.hh"

namespace tests {

	struct typed_key {

		char ascii;
		uint8_t scan;

	};

	static const typed_key plain_keys [ ] = {

		{ '1', 0x02 }, { '2', 0x03 }, { '3', 0x04 }, { '4', 0x05 }, { '5', 0x06 }, { '6', 0x07 }, { '7', 0x08 }, { '8', 0x09 },
		{ '9', 0x0A }, { '0', 0x0B }, { '-', 0x0C }, { '=', 0x0D }, { 'q', 0x10 }, { 'w', 0x11 }, { 'e', 0x12 }, { 'r', 0x13 },
		{ 't', 0x14 }, { 'y', 0x15 }, { 'u', 0x16 }, { 'i', 0x17 }, { 'o', 0x18 }, { 'p', 0x19 }, { '[', 0x1A }, { ']', 0x1B },
		{ 'a', 0x1E }, { 's', 0x1F }, { 'd', 0x20 }, { 'f', 0x21 }, { 'g', 0x22 }, { 'h', 0x23 }, { 'j', 0x24 }, { 'k', 0x25 },
		{ 'l', 0x26 }, { ';', 0x27 }, { '\'', 0x28 }, { '`', 0x29 }, { '\\', 0x2B }, { 'z', 0x2C }, { 'x', 0x2D }, { 'c', 0x2E },
		{ 'v', 0x2F }, { 'b', 0x30 }, { 'n', 0x31 }, { 'm', 0x32 }, { ',', 0x33 }, { '.', 0x34 }, { '/', 0x35 }, { ' ', 0x39 },

	};

	static const typed_key shifted_keys [ ] = {

		{ '!', 0x02 }, { '@', 0x03 }, { '#', 0x04 }, { '$', 0x05 }, { '%', 0x06 }, { '^', 0x07 }, { '&', 0x08 }, { '*', 0x09 },
		{ '(', 0x0A }, { ')', 0x0B }, { '_', 0x0C }, { '+', 0x0D }, { '{', 0x1A }, { '}', 0x1B }, { ':', 0x27 }, { '"', 0x28 },
		{ '~', 0x29 }, { '|', 0x2B }, { '<', 0x33 }, { '>', 0x34 }, { '?', 0x35 },

	};

	static bool find_key ( wchar_t value, uint8_t& scan, bool& shift ) {

		char ascii = static_cast < char > ( value >= L'A' && value <= L'Z' ? value - L'A' + L'a' : value );

		shift = value >= L'A' && value <= L'Z';

		for ( const typed_key& key : plain_keys ) {

			if ( key.ascii == ascii ) {

				scan = key.scan;

				return true;

			}

		}

		for ( const typed_key& key : shifted_keys ) {

			if ( key.ascii == ascii ) {

				scan = key.scan;
				shift = true;

				return true;

			}

		}

		return false;

	}

	static void press ( machine::xt& board, uint8_t scan ) {

		board.keys.press ( scan );
		board.run ( machine::xt::clock_rate / 25 );

	}

	static void type_text ( machine::xt& board, const wchar_t* text ) {

		for ( int32_t i = 0; text [ i ]; i++ ) {

			uint8_t scan = 0;
			bool shift = false;

			if ( text [ i ] == L'\\' && text [ i + 1 ] == L'n' ) {

				scan = 0x1C;
				i++;

			} else if ( !find_key ( text [ i ], scan, shift ) )
				continue;

			if ( shift )
				press ( board, 0x2A );

			press ( board, scan );
			press ( board, static_cast < uint8_t > ( scan | 0x80 ) );

			if ( shift )
				press ( board, 0xAA );

		}

	}

	static uint32_t parse_hex ( const wchar_t* text ) {

		uint32_t value = 0;

		for ( ; *text; text++ ) {

			wchar_t c = *text;
			uint32_t digit = c >= L'0' && c <= L'9' ? c - L'0' : ( c >= L'a' && c <= L'f' ? c - L'a' + 10 : ( c >= L'A' && c <= L'F' ? c - L'A' + 10 : 16 ) );

			if ( digit > 15 )
				break;

			value = value * 16 + digit;

		}

		return value;

	}

	static double parse_seconds ( const wchar_t* text ) {

		double value = 0.0;
		double scale = 0.0;

		for ( ; *text; text++ ) {

			if ( *text == L'.' && scale == 0.0 ) {

				scale = 0.1;

				continue;

			}

			if ( *text < L'0' || *text > L'9' )
				break;

			if ( scale == 0.0 )
				value = value * 10.0 + ( *text - L'0' );
			else {

				value += ( *text - L'0' ) * scale;
				scale *= 0.1;

			}

		}

		return value;

	}

	static void print_screen ( machine::xt& board ) {

		machine::cga& video = board.video;
		int32_t columns = base::min ( static_cast < int32_t > ( video.crtc [ 1 ] ), 80 );
		int32_t rows = base::min ( video.crtc [ 6 ] & 0x7F, 50 );
		uint32_t start = static_cast < uint32_t > ( video.crtc [ 12 ] << 8 | video.crtc [ 13 ] );
		char line [ 96 ];

		print ( "\r\n" );

		for ( int32_t row = 0; row < rows; row++ ) {

			int32_t length = 0;

			for ( int32_t column = 0; column < columns; column++ ) {

				uint32_t address = 0xB8000 + ( ( start + static_cast < uint32_t > ( row * columns + column ) ) * 2 & 0x3FFF );
				uint8_t value = board.memory.read8 ( address );

				line [ length++ ] = value >= 0x20 && value < 0x7F ? static_cast < char > ( value ) : ( value ? '.' : ' ' );

			}

			while ( length && line [ length - 1 ] == ' ' )
				length--;

			line [ length ] = 0;
			print ( line );
			print ( "\r\n" );

		}

	}

	static void print_state ( machine::xt& board ) {

		char line [ 256 ] = { };
		machine::registers& r = board.core.state;

		append ( line, 256, "CS:IP " );
		append_hex ( line, 256, r.segments [ machine::SEG_CS ], 4 );
		append ( line, 256, ":" );
		append_hex ( line, 256, r.ip, 4 );
		append ( line, 256, "  flags " );
		append_hex ( line, 256, r.flags, 4 );
		append ( line, 256, "  halted " );
		append ( line, 256, board.core.halted ? "yes" : "no" );
		append ( line, 256, "  pic irr " );
		append_hex ( line, 256, board.interrupts.irr, 2 );
		append ( line, 256, " isr " );
		append_hex ( line, 256, board.interrupts.isr, 2 );
		append ( line, 256, " imr " );
		append_hex ( line, 256, board.interrupts.imr, 2 );
		append ( line, 256, "  fdc phase " );
		append_number ( line, 256, board.floppy.phase );
		append ( line, 256, " dor " );
		append_hex ( line, 256, board.floppy.output, 2 );
		append ( line, 256, " cylinder " );
		append_number ( line, 256, board.floppy.drives [ 0 ].cylinder );
		append ( line, 256, "\r\n" );
		print ( line );

	}

	static void hunt ( machine::xt& board, double seconds ) {

		static const int32_t depth = 48;
		uint32_t trail [ depth ] = { };
		uint64_t steps = 0;
		uint64_t limit = board.core.cycles + static_cast < uint64_t > ( seconds * machine::xt::clock_rate );

		while ( board.core.cycles < limit ) {

			uint32_t at = board.core.instruction_address ( );

			trail [ steps++ % depth ] = static_cast < uint32_t > ( board.core.state.segments [ machine::SEG_CS ] ) << 16 | board.core.state.ip;

			if ( ( at >= 0xA0000 && at < 0xB8000 ) || ( at >= 0xC0000 && at < 0xFE000 ) )
				break;

			board.run ( 1 );

		}

		print ( "\r\n" );

		for ( uint64_t i = steps > depth ? steps - depth : 0; i < steps; i++ ) {

			char line [ 64 ] = { };
			uint32_t entry = trail [ i % depth ];
			uint32_t linear = machine::cpu::linear ( static_cast < uint16_t > ( entry >> 16 ), static_cast < uint16_t > ( entry ) );

			append_hex ( line, 64, entry >> 16, 4 );
			append ( line, 64, ":" );
			append_hex ( line, 64, entry & 0xFFFF, 4 );
			append ( line, 64, "  " );

			for ( uint32_t b = 0; b < 6; b++ ) {

				append_hex ( line, 64, board.memory.read8 ( linear + b ), 2 );
				append ( line, 64, " " );

			}

			append ( line, 64, "\r\n" );
			print ( line );

		}

		print_state ( board );

	}

	static bool save_shot ( machine::xt& board, const wchar_t* path ) {

		machine::cga& video = board.video;
		int32_t width = video.width * 2;
		int32_t height = width * 3 / 4;
		uint32_t* out = static_cast < uint32_t* > ( runtime_allocate ( sizeof ( uint32_t ) * static_cast < size_t > ( width ) * height ) );

		for ( int32_t y = 0; y < height; y++ ) {

			double top = static_cast < double > ( y ) * video.height / height;
			double bottom = static_cast < double > ( y + 1 ) * video.height / height;

			for ( int32_t x = 0; x < width; x++ ) {

				double sum [ 3 ] = { };

				for ( int32_t source = static_cast < int32_t > ( top ); source < video.height && source < bottom; source++ ) {

					double weight = base::min ( bottom, source + 1.0 ) - base::max ( top, static_cast < double > ( source ) );
					uint32_t value = video.pixels [ source * video.width + x / 2 ];

					sum [ 0 ] += weight * ( value >> 16 & 0xFF );
					sum [ 1 ] += weight * ( value >> 8 & 0xFF );
					sum [ 2 ] += weight * ( value & 0xFF );

				}

				double span = bottom - top;
				uint32_t red = static_cast < uint32_t > ( sum [ 0 ] / span + 0.5 );
				uint32_t green = static_cast < uint32_t > ( sum [ 1 ] / span + 0.5 );
				uint32_t blue = static_cast < uint32_t > ( sum [ 2 ] / span + 0.5 );

				out [ y * width + x ] = base::min ( red, 255u ) << 16 | base::min ( green, 255u ) << 8 | base::min ( blue, 255u );

			}

		}

		bool saved = png::save ( path, out, width, height );

		runtime_release ( out );

		return saved;

	}

	int32_t capture_main ( wchar_t** arguments, int32_t count ) {

		machine::xt* board = new machine::xt ( );
		int32_t problems = 0;

		for ( int32_t i = 1; i < count; i++ ) {

			const wchar_t* option = arguments [ i ];
			const wchar_t* value = i + 1 < count ? arguments [ i + 1 ] : L"";
			base::list < char > data;

			if ( base::same_text ( option, L"--bios" ) ) {

				i++;

				if ( !files::read ( value, data ) || !board->load_bios ( reinterpret_cast < const uint8_t* > ( data.data ( ) ), static_cast < uint32_t > ( data.count ( ) - 1 ) ) ) {

					print ( "capture: couldn't load the BIOS\r\n" );
					problems++;

				}

				board->power_on ( );

			} else if ( base::same_text ( option, L"--a" ) || base::same_text ( option, L"--b" ) ) {

				i++;

				if ( !files::read ( value, data ) || !board->floppy.insert ( option [ 2 ] == L'a' ? 0 : 1, reinterpret_cast < const uint8_t* > ( data.data ( ) ), static_cast < uint32_t > ( data.count ( ) - 1 ), false ) ) {

					print ( "capture: couldn't insert the disk\r\n" );
					problems++;

				}

			} else if ( base::same_text ( option, L"--run" ) ) {

				i++;
				board->run ( static_cast < uint64_t > ( parse_seconds ( value ) * machine::xt::clock_rate ) );

			} else if ( base::same_text ( option, L"--type" ) ) {

				i++;
				type_text ( *board, value );

			} else if ( base::same_text ( option, L"--key" ) ) {

				i++;
				press ( *board, static_cast < uint8_t > ( parse_hex ( value ) ) );

			} else if ( base::same_text ( option, L"--print" ) )
				print_screen ( *board );
			else if ( base::same_text ( option, L"--where" ) )
				print_state ( *board );
			else if ( base::same_text ( option, L"--hunt" ) ) {

				i++;
				hunt ( *board, parse_seconds ( value ) );

			}
			else if ( base::same_text ( option, L"--shot" ) ) {

				i++;

				if ( !save_shot ( *board, value ) ) {

					print ( "capture: couldn't write the screenshot\r\n" );
					problems++;

				}

			}

		}

		delete board;

		return problems;

	}

}

#endif