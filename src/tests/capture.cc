#ifdef COLDBOOT_DEBUG

#include "app/dialogs.hh"
#include "base/files.hh"
#include "base/png.hh"
#include "firmware/firmware.hh"
#include "machine/xt.hh"
#include "render/gpu.hh"
#include "storage/blank.hh"
#include "storage/program.hh"
#include "tests/harness.hh"
#include "ui/shell.hh"

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

	static void trail ( machine::xt& board, double seconds ) {

		static const int32_t depth = 64;
		uint32_t ring [ depth ] = { };
		uint64_t steps = 0;
		uint64_t limit = board.core.cycles + static_cast < uint64_t > ( seconds * machine::xt::clock_rate );
		bool derailed = false;

		while ( board.core.cycles < limit ) {

			uint16_t segment = board.core.state.segments [ machine::SEG_CS ];
			uint32_t at = board.core.instruction_address ( );

			ring [ steps++ % depth ] = static_cast < uint32_t > ( segment ) << 16 | board.core.state.ip;

			if ( segment != 0xF000 && segment != 0xFFFF && !( segment == 0 && at == 0x7C00 ) ) {

				derailed = true;

				break;

			}

			board.run ( 1 );

		}

		char head [ 48 ] = "\r\ntrail: ";

		append ( head, 48, derailed ? "derailed\r\n" : "ran clean\r\n" );
		print ( head );

		for ( uint64_t i = steps > depth ? steps - depth : 0; i < steps; i++ ) {

			char line [ 64 ] = { };
			uint32_t entry = ring [ i % depth ];
			uint32_t linear = machine::cpu::linear ( static_cast < uint16_t > ( entry >> 16 ), static_cast < uint16_t > ( entry ) );

			append_hex ( line, 64, entry >> 16, 4 );
			append ( line, 64, ":" );
			append_hex ( line, 64, entry & 0xFFFF, 4 );
			append ( line, 64, "  " );

			for ( uint32_t b = 0; b < 5; b++ ) {

				append_hex ( line, 64, board.memory.read8 ( linear + b ), 2 );
				append ( line, 64, " " );

			}

			append ( line, 64, "\r\n" );
			print ( line );

		}

	}

	static bool save_shot ( machine::xt& board, const wchar_t* path ) {

		return app::save_screenshot ( board.video.pixels, board.video.width, board.video.height, path );

	}

	struct interface_options {

		bool light = false;
		bool crt = false;
		bool turbo = false;
		bool captured = false;
		bool off = false;
		bool post = false;
		bool fast = false;
		ui::popover_kind panel = ui::POPOVER_NONE;
		int32_t width = 1040;
		int32_t height = 860;
		float scale = 1.0f;
		ui::control hover = ui::CONTROL_NONE;
		wchar_t toast [ 96 ] = { };

	};

	static bool save_interface ( machine::xt& board, const interface_options& options, wchar_t names [ 2 ] [ MAX_PATH ], const wchar_t* path ) {

		render::gpu* gpu = new render::gpu ( );
		ui::shell* shell = new ui::shell ( );
		int32_t pixel_width = static_cast < int32_t > ( options.width * options.scale );
		int32_t pixel_height = static_cast < int32_t > ( options.height * options.scale );
		bool saved = false;

		if ( gpu->create ( nullptr, pixel_width, pixel_height ) ) {

			ui::view state;

			gpu->set_dpi ( 96.0f * options.scale );
			shell->layout ( static_cast < float > ( options.width ), static_cast < float > ( options.height ), false );
			state.pixels = options.off ? nullptr : board.video.pixels;
			state.frame_width = board.video.width;
			state.frame_height = board.video.height;
			state.powered = !options.off;
			state.turbo = options.turbo;
			state.crt = options.crt;
			state.light = options.light;
			state.fast_floppy = options.fast;
			state.captured = options.captured;
			state.can_load = true;
			state.post_code = options.post ? board.pages.post_code : 0;
			state.speed = options.turbo ? 38.4 : 4.77;
			state.memory = board.memory_kilobytes;
			state.bios_name = L"Coldboot BIOS";

			for ( int32_t d = 0; d < 2; d++ ) {

				const machine::floppy& disk = board.floppy.drives [ d ].disk;

				state.drives [ d ].name = names [ d ];
				state.drives [ d ].present = disk.present ( );
				state.drives [ d ].kilobytes = disk.present ( ) ? disk.format->size / 1024 : 0;
				state.drives [ d ].protect = disk.protect;
				state.drives [ d ].light = board.floppy.spinning ( d );
				state.drives [ d ].program = disk.present ( ) && machine::is_program_path ( names [ d ] );

			}

			double now = 10.0;

			for ( int32_t i = 0; i < 4; i++ ) {

				shell->paint ( *gpu, state, now );
				gpu->end ( );
				now += 0.05;

			}

			if ( options.panel != ui::POPOVER_NONE )
				shell->open_popover ( options.panel, now );

			if ( options.hover != ui::CONTROL_NONE ) {

				render::rect area = shell->anchor_of ( options.hover );

				shell->mouse_move ( area.x + area.w * 0.5f, area.y + area.h * 0.5f, now );

			}

			if ( options.toast [ 0 ] )
				shell->toast ( options.toast, now - 0.6 );

			for ( int32_t i = 0; i < 40; i++ ) {

				shell->paint ( *gpu, state, now );
				gpu->end ( );
				now += 0.05;

			}

			saved = gpu->save ( path );

		}

		delete shell;
		delete gpu;

		return saved;

	}

	static ui::control control_named ( const wchar_t* name ) {

		struct entry {

			const wchar_t* name;
			ui::control id;

		};

		static const entry entries [ ] = {

			{ L"power", ui::CONTROL_POWER }, { L"reset", ui::CONTROL_RESET }, { L"pause", ui::CONTROL_PAUSE }, { L"save", ui::CONTROL_SAVE },
			{ L"load", ui::CONTROL_LOAD }, { L"shot", ui::CONTROL_SHOT }, { L"speed", ui::CONTROL_SPEED }, { L"disks", ui::CONTROL_DISKS },
			{ L"settings", ui::CONTROL_SETTINGS }, { L"fullscreen", ui::CONTROL_FULLSCREEN },

		};

		for ( const entry& item : entries ) {

			if ( base::same_text ( item.name, name ) )
				return item.id;

		}

		return ui::CONTROL_NONE;

	}

	int32_t capture_main ( wchar_t** arguments, int32_t count ) {

		machine::xt* board = new machine::xt ( );
		int32_t problems = 0;
		interface_options look;
		static wchar_t names [ 2 ] [ MAX_PATH ];

		names [ 0 ] [ 0 ] = 0;
		names [ 1 ] [ 0 ] = 0;

		for ( int32_t i = 1; i < count; i++ ) {

			const wchar_t* option = arguments [ i ];
			const wchar_t* value = i + 1 < count ? arguments [ i + 1 ] : L"";
			base::list < char > data;

			if ( base::same_text ( option, L"--asm" ) ) {

				base::list < uint8_t > rom;
				char message [ 200 ] = { };
				char result [ 260 ] = "asm: ";

				if ( firmware::build ( rom, message, sizeof ( message ), nullptr ) ) {

					append ( result, 260, "ok, " );
					append_number ( result, 260, rom.count ( ) );
					append ( result, 260, " bytes\r\n" );

				} else {

					append ( result, 260, message );
					append ( result, 260, "\r\n" );
					problems++;

				}

				print ( result );

			} else if ( base::same_text ( option, L"--own" ) ) {

				char message [ 200 ] = { };

				if ( !board->load_builtin ( message, sizeof ( message ), nullptr ) ) {

					char report [ 260 ] = "capture: ";

					append ( report, 260, message );
					append ( report, 260, "\r\n" );
					print ( report );
					problems++;

				}

				board->power_on ( );

			} else if ( base::same_text ( option, L"--bios" ) ) {

				i++;

				if ( !files::read ( value, data ) || !board->load_bios ( reinterpret_cast < const uint8_t* > ( data.data ( ) ), static_cast < uint32_t > ( data.count ( ) - 1 ) ) ) {

					print ( "capture: couldn't load the BIOS\r\n" );
					problems++;

				}

				board->power_on ( );

			} else if ( base::same_text ( option, L"--a" ) || base::same_text ( option, L"--b" ) ) {

				i++;

				int32_t drive = option [ 2 ] == L'a' ? 0 : 1;
				base::list < uint8_t > built;
				bool ready = files::read ( value, data );
				const uint8_t* image = reinterpret_cast < const uint8_t* > ( data.data ( ) );
				uint32_t size = ready ? static_cast < uint32_t > ( data.count ( ) - 1 ) : 0;

				if ( ready && machine::is_program_path ( value ) ) {

					char report [ 220 ] = "asm: ";
					char message [ 160 ] = { };
					uint32_t used = 0;

					ready = machine::make_program_disk ( data.data ( ), static_cast < int32_t > ( size ), built, used, message, sizeof ( message ) );

					if ( ready ) {

						append ( report, 220, "ok, " );
						append_number ( report, 220, used );
						append ( report, 220, " bytes\r\n" );
						image = built.data ( );
						size = static_cast < uint32_t > ( built.count ( ) );

					} else {

						append ( report, 220, message );
						append ( report, 220, "\r\n" );

					}

					print ( report );

				}

				if ( !ready || !board->floppy.insert ( drive, image, size, false ) ) {

					print ( "capture: couldn't insert the disk\r\n" );
					problems++;

				} else {

					const wchar_t* tail = value;

					for ( const wchar_t* at = value; *at; at++ ) {

						if ( *at == L'\\' || *at == L'/' )
							tail = at + 1;

					}

					int32_t length = 0;

					for ( ; tail [ length ] && length < MAX_PATH - 1; length++ )
						names [ drive ] [ length ] = tail [ length ];

					names [ drive ] [ length ] = 0;

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

			} else if ( base::same_text ( option, L"--trail" ) ) {

				i++;
				trail ( *board, parse_seconds ( value ) );

			} else if ( base::same_text ( option, L"--shot" ) ) {

				i++;

				if ( !save_shot ( *board, value ) ) {

					print ( "capture: couldn't write the screenshot\r\n" );
					problems++;

				}

			} else if ( base::same_text ( option, L"--blank" ) && i + 2 < count ) {

				base::list < uint8_t > image;
				uint32_t size = static_cast < uint32_t > ( parse_seconds ( arguments [ i + 1 ] ) );

				if ( !machine::make_blank_disk ( size, 0x1234ABCD, image ) || !files::write ( arguments [ i + 2 ], reinterpret_cast < const char* > ( image.data ( ) ), image.count ( ) ) ) {

					print ( "capture: couldn't make the blank disk\r\n" );
					problems++;

				}

				i += 2;

			} else if ( base::same_text ( option, L"--fast" ) ) {

				board->floppy.fast = true;
				look.fast = true;

			} else if ( base::same_text ( option, L"--ui-light" ) )
				look.light = true;
			else if ( base::same_text ( option, L"--ui-dark" ) )
				look.light = false;
			else if ( base::same_text ( option, L"--ui-crt" ) )
				look.crt = true;
			else if ( base::same_text ( option, L"--ui-sharp" ) )
				look.crt = false;
			else if ( base::same_text ( option, L"--ui-turbo" ) )
				look.turbo = true;
			else if ( base::same_text ( option, L"--ui-captured" ) )
				look.captured = true;
			else if ( base::same_text ( option, L"--ui-off" ) )
				look.off = true;
			else if ( base::same_text ( option, L"--ui-post" ) )
				look.post = true;
			else if ( base::same_text ( option, L"--ui-reset" ) )
				look = interface_options ( );
			else if ( base::same_text ( option, L"--ui-panel" ) ) {

				i++;
				look.panel = base::same_text ( value, L"disks" ) ? ui::POPOVER_DISKS : ( base::same_text ( value, L"settings" ) ? ui::POPOVER_SETTINGS : ui::POPOVER_NONE );

			} else if ( base::same_text ( option, L"--ui-hover" ) ) {

				i++;
				look.hover = control_named ( value );

			} else if ( base::same_text ( option, L"--ui-scale" ) ) {

				i++;
				look.scale = static_cast < float > ( parse_seconds ( value ) );

			} else if ( base::same_text ( option, L"--ui-size" ) && i + 2 < count ) {

				look.width = static_cast < int32_t > ( parse_seconds ( arguments [ i + 1 ] ) );
				look.height = static_cast < int32_t > ( parse_seconds ( arguments [ i + 2 ] ) );
				i += 2;

			} else if ( base::same_text ( option, L"--ui-toast" ) ) {

				i++;

				int32_t length = 0;

				for ( ; value [ length ] && length < 95; length++ )
					look.toast [ length ] = value [ length ];

				look.toast [ length ] = 0;

			} else if ( base::same_text ( option, L"--ui" ) ) {

				i++;

				if ( !save_interface ( *board, look, names, value ) ) {

					print ( "capture: couldn't render the interface\r\n" );
					problems++;

				}

			}

		}

		delete board;

		return problems;

	}

}

#endif