#ifdef COLDBOOT_DEBUG

#include "base/png.hh"
#include "pc/computer.hh"
#include "pc/iso.hh"
#include "tests/harness.hh"
#include <windows.h>

namespace tests {

	static const char letters [ 26 ] = { 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C };
	static const char digits [ 10 ] = { 0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A };

	static void press ( pc::computer& machine, uint8_t scan, bool shifted ) {

		if ( shifted )
			machine.key ( 0x2A, false );

		machine.key ( scan, false );
		Sleep ( 40 );
		machine.key ( static_cast < uint8_t > ( scan | 0x80 ), false );

		if ( shifted )
			machine.key ( 0xAA, false );

		Sleep ( 40 );

	}

	static void type_text ( pc::computer& machine, const wchar_t* text ) {

		for ( int32_t i = 0; text [ i ]; i++ ) {

			wchar_t c = text [ i ];

			if ( c == L'\\' && text [ i + 1 ] == L'n' ) {

				press ( machine, 0x1C, false );
				i++;

			} else if ( c == L'\\' && text [ i + 1 ] == L'e' ) {

				press ( machine, 0x01, false );
				i++;

			} else if ( c == L'\\' && text [ i + 1 ] == L'd' ) {

				machine.key ( 0x50, true );
				Sleep ( 40 );
				machine.key ( 0xD0, true );
				Sleep ( 40 );
				i++;

			} else if ( c == L'\\' && text [ i + 1 ] == L'u' ) {

				machine.key ( 0x48, true );
				Sleep ( 40 );
				machine.key ( 0xC8, true );
				Sleep ( 40 );
				i++;

			} else if ( c >= L'a' && c <= L'z' )
				press ( machine, static_cast < uint8_t > ( letters [ c - L'a' ] ), false );
			else if ( c >= L'A' && c <= L'Z' )
				press ( machine, static_cast < uint8_t > ( letters [ c - L'A' ] ), true );
			else if ( c >= L'0' && c <= L'9' )
				press ( machine, static_cast < uint8_t > ( digits [ c - L'0' ] ), false );
			else if ( c == L' ' )
				press ( machine, 0x39, false );
			else if ( c == L'.' )
				press ( machine, 0x34, false );
			else if ( c == L':' )
				press ( machine, 0x27, true );
			else if ( c == L'\\' )
				press ( machine, 0x2B, false );
			else if ( c == L'-' )
				press ( machine, 0x0C, false );
			else if ( c == L'/' )
				press ( machine, 0x35, false );
			else if ( c == L'=' )
				press ( machine, 0x0D, false );
			else if ( c == L',' )
				press ( machine, 0x33, false );
			else if ( c == L'\t' )
				press ( machine, 0x0F, false );

		}

	}

	static double now_seconds ( ) {

		LARGE_INTEGER frequency;
		LARGE_INTEGER counter;

		QueryPerformanceFrequency ( &frequency );
		QueryPerformanceCounter ( &counter );

		return static_cast < double > ( counter.QuadPart ) / static_cast < double > ( frequency.QuadPart );

	}

	static void save_screen ( pc::computer& machine, const wchar_t* folder, const wchar_t* name, uint32_t* pixels ) {

		int32_t width = 0;
		int32_t height = 0;

		if ( !machine.screen ( pixels, width, height, now_seconds ( ) ) )
			return;

		base::writer path;

		path.add ( folder ).add ( L"\\" ).add ( name );
		png::save ( path.text ( ), pixels, width, height );

	}

	static void add_tree ( pc::iso_builder& builder, const wchar_t* folder, const char* prefix, int32_t& added ) {

		base::writer pattern;
		WIN32_FIND_DATAW found;

		pattern.add ( folder ).add ( L"\\*" );

		HANDLE search = FindFirstFileW ( pattern.text ( ), &found );

		if ( search == INVALID_HANDLE_VALUE )
			return;

		do {

			if ( found.cFileName [ 0 ] == L'.' )
				continue;

			char name [ 260 ];
			char path [ 520 ];
			base::writer full;
			int32_t length = WideCharToMultiByte ( CP_UTF8, 0, found.cFileName, -1, name, sizeof ( name ), nullptr, nullptr );

			if ( length <= 0 )
				continue;

			path [ 0 ] = 0;

			if ( prefix [ 0 ] ) {

				append ( path, sizeof ( path ), prefix );
				append ( path, sizeof ( path ), "/" );

			}

			append ( path, sizeof ( path ), name );
			full.add ( folder ).add ( L"\\" ).add ( found.cFileName );

			if ( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
				add_tree ( builder, full.text ( ), path, added );
			else if ( builder.add_file ( path, full.text ( ) ) )
				added++;

		} while ( FindNextFileW ( search, &found ) );

		FindClose ( search );

	}

	int32_t make_iso_main ( wchar_t** arguments, int32_t count ) {

		const wchar_t* target = nullptr;
		const wchar_t* folder = nullptr;
		pc::iso_builder* builder = new pc::iso_builder ( );
		char boot [ 260 ] = { };
		uint16_t sectors = 4;

		for ( int32_t i = 1; i < count; i++ ) {

			if ( base::same_text ( arguments [ i ], L"--make-iso" ) && i + 2 < count ) {

				target = arguments [ ++i ];
				folder = arguments [ ++i ];

			} else if ( base::same_text ( arguments [ i ], L"--boot" ) && i + 1 < count )
				WideCharToMultiByte ( CP_UTF8, 0, arguments [ ++i ], -1, boot, sizeof ( boot ), nullptr, nullptr );
			else if ( base::same_text ( arguments [ i ], L"--boot-sectors" ) && i + 1 < count ) {

				sectors = 0;

				for ( const wchar_t* at = arguments [ ++i ]; *at >= L'0' && *at <= L'9'; at++ )
					sectors = static_cast < uint16_t > ( sectors * 10 + ( *at - L'0' ) );

			} else if ( base::same_text ( arguments [ i ], L"--label" ) && i + 1 < count ) {

				char text [ 64 ] = { };

				WideCharToMultiByte ( CP_UTF8, 0, arguments [ ++i ], -1, text, sizeof ( text ), nullptr, nullptr );
				builder->set_label ( text );

			}

		}

		if ( !target || !folder ) {

			print ( "--make-iso needs a target file and a source folder\r\n" );
			delete builder;

			return 1;

		}

		int32_t added = 0;
		char line [ 256 ] = { };
		char error [ 160 ] = { };

		add_tree ( *builder, folder, "", added );

		if ( boot [ 0 ] )
			builder->set_boot ( boot, sectors );

		bool ok = builder->write ( target, error, sizeof ( error ) );

		append ( line, sizeof ( line ), ok ? "wrote the ISO with " : "failed: " );

		if ( ok ) {

			append_number ( line, sizeof ( line ), added );
			append ( line, sizeof ( line ), " files\r\n" );

		} else {

			append ( line, sizeof ( line ), error );
			append ( line, sizeof ( line ), "\r\n" );

		}

		print ( line );
		delete builder;

		return ok ? 0 : 1;

	}

	int32_t computer_main ( wchar_t** arguments, int32_t count ) {

		const wchar_t* folder = nullptr;
		const wchar_t* typed = nullptr;
		double type_at = 0.0;
		const wchar_t* script_text [ 32 ] = { };
		double script_at [ 32 ] = { };
		int32_t script_count = 0;
		int32_t script_next = 0;
		double seconds = 20.0;
		double every = 5.0;
		pc::computer_options* options = new pc::computer_options ( );

		for ( int32_t i = 1; i < count; i++ ) {

			auto next = [ & ] ( ) {

				return i + 1 < count ? arguments [ ++i ] : L"";

			};

			auto number = [ ] ( const wchar_t* text ) {

				double value = 0.0;
				double scale = 0.0;

				for ( int32_t c = 0; text [ c ]; c++ ) {

					if ( text [ c ] == L'.' )
						scale = 1.0;
					else if ( text [ c ] >= L'0' && text [ c ] <= L'9' ) {

						if ( scale > 0.0 ) {

							scale /= 10.0;
							value += ( text [ c ] - L'0' ) * scale;

						} else
							value = value * 10.0 + ( text [ c ] - L'0' );

					}

				}

				return value;

			};

			if ( base::same_text ( arguments [ i ], L"--pc" ) )
				folder = next ( );
			else if ( base::same_text ( arguments [ i ], L"--iso" ) ) {

				const wchar_t* path = next ( );

				for ( int32_t c = 0; c < MAX_PATH - 1 && path [ c ]; c++ )
					options->iso [ c ] = path [ c ];

			} else if ( base::same_text ( arguments [ i ], L"--disk" ) ) {

				const wchar_t* path = next ( );

				for ( int32_t c = 0; c < MAX_PATH - 1 && path [ c ]; c++ )
					options->disk [ c ] = path [ c ];

			} else if ( base::same_text ( arguments [ i ], L"--memory" ) )
				options->memory = static_cast < uint64_t > ( number ( next ( ) ) ) << 20;
			else if ( base::same_text ( arguments [ i ], L"--cpus" ) )
				options->processors = static_cast < uint32_t > ( number ( next ( ) ) );
			else if ( base::same_text ( arguments [ i ], L"--seconds" ) )
				seconds = number ( next ( ) );
			else if ( base::same_text ( arguments [ i ], L"--every" ) )
				every = number ( next ( ) );
			else if ( base::same_text ( arguments [ i ], L"--disk-first" ) )
				options->optical_first = false;
			else if ( base::same_text ( arguments [ i ], L"--type" ) )
				typed = next ( );
			else if ( base::same_text ( arguments [ i ], L"--at" ) )
				type_at = number ( next ( ) );
			else if ( base::same_text ( arguments [ i ], L"--script" ) && i + 2 < count && script_count < 32 ) {

				script_at [ script_count ] = number ( next ( ) );
				script_text [ script_count ] = next ( );
				script_count++;

			}

		}

		if ( !folder ) {

			print ( "--pc needs an output folder\r\n" );
			delete options;

			return 1;

		}

		CreateDirectoryW ( folder, nullptr );

		pc::computer* machine = new pc::computer ( );
		uint32_t* pixels = static_cast < uint32_t* > ( runtime_allocate ( sizeof ( uint32_t ) * pc::vga::max_width * pc::vga::max_height ) );
		char line [ 512 ];

		if ( !machine->power_on ( *options ) ) {

			line [ 0 ] = 0;
			append ( line, sizeof ( line ), "power on failed: " );
			append ( line, sizeof ( line ), machine->problem );
			append ( line, sizeof ( line ), "\r\n" );
			print ( line );
			runtime_release ( pixels );
			delete machine;
			delete options;

			return 1;

		}

		double started = now_seconds ( );
		double next_shot = every;
		int32_t shot = 0;
		bool typed_done = typed == nullptr;
		int32_t resets = 0;

		while ( now_seconds ( ) - started < seconds ) {

			Sleep ( 50 );

			double elapsed = now_seconds ( ) - started;
			pc::computer_event event = machine->poll ( );

			if ( event == pc::EVENT_RESET ) {

				print ( "guest reset\r\n" );

				if ( ++resets > 5 || !machine->restart ( ) )
					break;

			} else if ( event == pc::EVENT_OFF ) {

				print ( "guest powered off\r\n" );

				break;

			} else if ( event == pc::EVENT_FAILED ) {

				line [ 0 ] = 0;
				append ( line, sizeof ( line ), "machine stopped: " );
				append ( line, sizeof ( line ), machine->problem );
				append ( line, sizeof ( line ), "\r\n" );
				print ( line );

				break;

			}

			if ( !typed_done && elapsed >= type_at ) {

				type_text ( *machine, typed );
				typed_done = true;

			}

			if ( script_next < script_count && elapsed >= script_at [ script_next ] ) {

				base::writer name;

				name.add ( L"step_" ).number ( script_next ).add ( L".png" );
				save_screen ( *machine, folder, name.text ( ), pixels );
				type_text ( *machine, script_text [ script_next ] );
				script_next++;

			}

			if ( every > 0.0 && elapsed >= next_shot ) {

				base::writer name;

				name.add ( L"shot_" ).number ( shot++ ).add ( L".png" );
				save_screen ( *machine, folder, name.text ( ), pixels );
				next_shot += every;

			}

		}

		save_screen ( *machine, folder, L"final.png", pixels );

		static char console [ 65536 ];

		machine->console_text ( console, sizeof ( console ) );
		line [ 0 ] = 0;
		append ( line, sizeof ( line ), "boot: " );
		append ( line, sizeof ( line ), machine->firmware ? machine->firmware->last_boot : "-" );
		append ( line, sizeof ( line ), "\r\nvideo mode " );
		append_hex ( line, sizeof ( line ), machine->video ? machine->video->legacy_mode : 0, 2 );
		append ( line, sizeof ( line ), " vbe " );
		append_hex ( line, sizeof ( line ), machine->video ? machine->video->vbe_number : 0, 4 );

		if ( machine->board ) {

			hv::cpu_state cpu;

			append ( line, sizeof ( line ), "\r\nmsr exits " );
			append_number ( line, sizeof ( line ), machine->board->vm.msr_exits );
			append ( line, sizeof ( line ), ", legacy interrupts " );
			append_number ( line, sizeof ( line ), machine->board->vm.injected );

			if ( machine->board->vm.pause ( ) ) {

				machine->board->vm.get_state ( 0, cpu );
				machine->board->vm.resume ( );
				append ( line, sizeof ( line ), "\r\ncs:ip " );
				append_hex ( line, sizeof ( line ), cpu.segments [ hv::SEG_CS ].selector, 4 );
				append ( line, sizeof ( line ), ":" );
				append_hex ( line, sizeof ( line ), static_cast < uint32_t > ( cpu.rip ), 8 );
				append ( line, sizeof ( line ), " cr0 " );
				append_hex ( line, sizeof ( line ), static_cast < uint32_t > ( cpu.cr0 ), 8 );

			}

		}

		append ( line, sizeof ( line ), "\r\nconsole:\r\n" );
		print ( line );
		print ( console );
		print ( "\r\n" );
		machine->power_off ( );
		runtime_release ( pixels );
		delete machine;
		delete options;

		return 0;

	}

}

#endif