#ifdef COLDBOOT_DEBUG

#include "base/gif.hh"
#include "base/png.hh"
#include "pc/computer.hh"
#include "pc/iso.hh"
#include "tests/harness.hh"
#include "vm/config.hh"
#include "vm/unattend.hh"
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

			} else if ( c == L'\\' && text [ i + 1 ] == L't' ) {

				press ( machine, 0x0F, false );
				i++;

			} else if ( c == L'\\' && text [ i + 1 ] == L'a' && text [ i + 2 ] >= L'a' && text [ i + 2 ] <= L'z' ) {

				machine.key ( 0x38, false );
				Sleep ( 40 );
				press ( machine, static_cast < uint8_t > ( letters [ text [ i + 2 ] - L'a' ] ), false );
				machine.key ( 0xB8, false );
				Sleep ( 40 );
				i += 2;

			} else if ( c == L'\\' && text [ i + 1 ] == L'w' ) {

				Sleep ( 1000 );
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

			} else if ( c == L'\\' && text [ i + 1 ] == L'b' ) {

				press ( machine, 0x0E, false );
				i++;

			} else if ( c == L'\\' && text [ i + 1 ] == L'g' ) {

				machine.key ( 0x5B, true );
				Sleep ( 60 );
				machine.key ( 0xDB, true );
				Sleep ( 40 );
				i++;

			} else if ( c == L'\\' && text [ i + 1 ] == L'R' ) {

				machine.key ( 0x5B, true );
				Sleep ( 40 );
				press ( machine, 0x13, false );
				machine.key ( 0xDB, true );
				Sleep ( 40 );
				i++;

			} else if ( c == L'\\' && text [ i + 1 ] == L'c' && text [ i + 2 ] >= L'a' && text [ i + 2 ] <= L'z' ) {

				machine.key ( 0x1D, false );
				Sleep ( 40 );
				press ( machine, static_cast < uint8_t > ( letters [ text [ i + 2 ] - L'a' ] ), false );
				machine.key ( 0x9D, false );
				Sleep ( 40 );
				i += 2;

			} else if ( c == L'\\' && text [ i + 1 ] == L'T' ) {

				machine.key ( 0x2A, false );
				press ( machine, 0x0F, false );
				machine.key ( 0xAA, false );
				Sleep ( 40 );
				i++;

			} else if ( c == L'\\' && text [ i + 1 ] == L's' ) {

				press ( machine, 0x39, false );
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
			else {

				static const wchar_t shifted [ ] = L"!@#$%^&*()_+{}\"~|<>?";
				static const uint8_t scans [ ] = { 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x1A, 0x1B, 0x28, 0x29, 0x2B, 0x33, 0x34, 0x35 };
				static const wchar_t plain [ ] = L"[]'`;";
				static const uint8_t plain_scans [ ] = { 0x1A, 0x1B, 0x28, 0x29, 0x27 };

				for ( int32_t k = 0; shifted [ k ]; k++ ) {

					if ( shifted [ k ] == c )
						press ( machine, scans [ k ], true );

				}

				for ( int32_t k = 0; plain [ k ]; k++ ) {

					if ( plain [ k ] == c )
						press ( machine, plain_scans [ k ], false );

				}

			}

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

	struct counting_speaker : pc::sound_sink {

		uint32_t frames = 0;
		float peak = 0.0f;

		void play ( const float* samples, uint32_t count, uint32_t ) override {

			frames += count;

			for ( uint32_t i = 0; i < count * 2; i++ ) {

				float level = samples [ i ] < 0.0f ? -samples [ i ] : samples [ i ];

				if ( level > peak )
					peak = level;

			}

		}

	};

	static bool starts ( const wchar_t* text, const wchar_t* prefix ) {

		for ( ; *prefix; text++, prefix++ ) {

			if ( *text != *prefix )
				return false;

		}

		return true;

	}

	static int32_t read_number ( const wchar_t*& at ) {

		int32_t value = 0;

		while ( *at == L' ' )
			at++;

		while ( *at >= L'0' && *at <= L'9' )
			value = value * 10 + ( *at++ - L'0' );

		return value;

	}

	static uint32_t waiting_ids [ 2048 ] = { };
	static int32_t waiting_count = 0;
	static bool trusting = false;

	static bool run_control ( pc::computer& machine, const wchar_t* path, const wchar_t* folder, uint32_t* pixels ) {

		HANDLE file = CreateFileW ( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr );

		if ( file == INVALID_HANDLE_VALUE )
			return true;

		static char bytes [ 8192 ];
		static wchar_t text [ 8192 ];
		DWORD got = 0;

		ReadFile ( file, bytes, sizeof ( bytes ) - 1, &got, nullptr );
		CloseHandle ( file );
		DeleteFileW ( path );

		int32_t length = MultiByteToWideChar ( CP_UTF8, 0, bytes, static_cast < int > ( got ), text, 8191 );

		text [ length > 0 ? length : 0 ] = 0;

		for ( wchar_t* line = text; *line; ) {

			wchar_t* end = line;

			while ( *end && *end != L'\n' && *end != L'\r' )
				end++;

			wchar_t saved = *end;

			*end = 0;

			if ( line [ 0 ] == 0xFEFF )
				line++;

			int32_t width = 0;
			int32_t height = 0;

			machine.screen ( pixels, width, height, now_seconds ( ) );

			if ( starts ( line, L"keys " ) )
				type_text ( machine, line + 5 );
			else if ( starts ( line, L"shot " ) )
				save_screen ( machine, folder, line + 5, pixels );
			else if ( starts ( line, L"wait " ) ) {

				const wchar_t* at = line + 5;

				Sleep ( static_cast < DWORD > ( read_number ( at ) ) );

			} else if ( starts ( line, L"click " ) || starts ( line, L"right " ) || starts ( line, L"move " ) || starts ( line, L"double " ) ) {

				const wchar_t* at = line + ( line [ 0 ] == L'm' ? 5 : ( line [ 0 ] == L'd' ? 7 : 6 ) );
				int32_t x = read_number ( at );
				int32_t y = read_number ( at );
				int32_t span = pc::usb_tablet::span;
				int32_t tablet_x = width ? static_cast < int32_t > ( ( static_cast < int64_t > ( x ) * span + span / 2 ) / width ) : 0;
				int32_t tablet_y = height ? static_cast < int32_t > ( ( static_cast < int64_t > ( y ) * span + span / 2 ) / height ) : 0;
				uint8_t button = line [ 0 ] == L'r' ? 2 : 1;

				machine.pointer ( tablet_x, tablet_y, 0, 0 );
				Sleep ( 60 );

				for ( int32_t times = line [ 0 ] == L'd' ? 2 : ( line [ 0 ] == L'm' ? 0 : 1 ); times > 0; times-- ) {

					machine.pointer ( tablet_x, tablet_y, 0, button );
					Sleep ( 60 );
					machine.pointer ( tablet_x, tablet_y, 0, 0 );
					Sleep ( 60 );

				}

			} else if ( starts ( line, L"tools" ) ) {

				wchar_t self [ MAX_PATH ] = { };
				base::writer target;
				char error [ 160 ] = { };
				pc::iso_builder* builder = new pc::iso_builder ( );

				GetModuleFileNameW ( nullptr, self, MAX_PATH );
				target.add ( folder ).add ( L"\\tools.iso" );
				builder->set_label ( "COLDBOOT_TOOLS" );

				bool made = builder->add_file ( "Coldboot Tools.exe", self ) && builder->write ( target.text ( ), error, sizeof ( error ) );

				delete builder;
				print ( made && machine.change_second_disc ( target.text ( ) ) ? "tools: disc inserted\r\n" : "tools: disc failed\r\n" );

			} else if ( starts ( line, L"clip " ) ) {

				int32_t length = 0;

				while ( line [ 5 + length ] )
					length++;

				print ( machine.tools.send_text ( line + 5, static_cast < uint32_t > ( length ) ) ? "tools: text sent\r\n" : "tools: text refused\r\n" );

			} else if ( starts ( line, L"clipfiles " ) || starts ( line, L"sendfile " ) ) {

				bool clipboard = starts ( line, L"clipfiles " );
				wchar_t paths [ 1 ] [ MAX_PATH ] = { };
				const wchar_t* source = line + ( clipboard ? 10 : 9 );

				for ( int32_t c = 0; source [ c ] && c < MAX_PATH - 1; c++ )
					paths [ 0 ] [ c ] = source [ c ];

				print ( machine.tools.send_files ( paths, 1, clipboard ? pc::PURPOSE_CLIPBOARD : pc::PURPOSE_SENT, ~0ull ) ? "tools: files sending\r\n" : "tools: files refused\r\n" );

			} else if ( starts ( line, L"allow" ) || starts ( line, L"block" ) ) {

				for ( int32_t i = 0; i < waiting_count && machine.router; i++ )
					machine.router->decide ( waiting_ids [ i ], line [ 0 ] == L'a' );

				char note [ 96 ];

				wsprintfA ( note, "ask: %s %d, replayed so far %d\r\n", line [ 0 ] == L'a' ? "allowed" : "blocked", waiting_count, machine.router ? static_cast < int > ( machine.router->replayed ) : 0 );
				print ( note );
				waiting_count = 0;

			} else if ( starts ( line, L"trust" ) ) {

				trusting = true;

				for ( int32_t i = 0; i < waiting_count && machine.router; i++ )
					machine.router->decide ( waiting_ids [ i ], true );

				waiting_count = 0;
				print ( "ask: trusting everything from now on\r\n" );

			} else if ( starts ( line, L"stats" ) && machine.router ) {

				char note [ 240 ];

				wsprintfA ( note, "nat: links %u, connects %d, failed %d, resets %d, replayed %d, blocked %d, handshakes %d, unanswered %d, guest resets %d\r\n", machine.router->links_in_use ( ), static_cast < int > ( machine.router->connects ), static_cast < int > ( machine.router->connect_failures ), static_cast < int > ( machine.router->resets ), static_cast < int > ( machine.router->replayed ), static_cast < int > ( machine.router->blocked ), static_cast < int > ( machine.router->handshakes ), static_cast < int > ( machine.router->unanswered ), static_cast < int > ( machine.router->guest_resets ) );
				print ( note );

			} else if ( starts ( line, L"power" ) )
				machine.press_power ( );
			else if ( starts ( line, L"quit" ) )
				return false;

			*end = saved;
			line = end;

			while ( *line == L'\n' || *line == L'\r' )
				line++;

		}

		return true;

	}

	int32_t computer_main ( wchar_t** arguments, int32_t count ) {

		const wchar_t* folder = nullptr;
		const wchar_t* control = nullptr;
		const wchar_t* unattend_user = nullptr;
		const wchar_t* unattend_password = L"";
		const wchar_t* typed = nullptr;
		double type_at = 0.0;
		const wchar_t* script_text [ 32 ] = { };
		double script_at [ 32 ] = { };
		int32_t script_count = 0;
		int32_t script_next = 0;
		double seconds = 20.0;
		double every = 5.0;
		const wchar_t* record_path = nullptr;
		double record_every = 0.1;
		double record_speed = 1.0;
		double record_from = 0.0;
		int32_t record_width = 640;
		int32_t record_height = 480;
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
			else if ( base::same_text ( arguments [ i ], L"--no-network" ) )
				options->network = false;
			else if ( base::same_text ( arguments [ i ], L"--uefi" ) )
				options->uefi = true;
			else if ( base::same_text ( arguments [ i ], L"--disposable" ) )
				options->disposable = true;
			else if ( base::same_text ( arguments [ i ], L"--fresh-identity" ) )
				options->fresh_identity = true;
			else if ( base::same_text ( arguments [ i ], L"--no-lan" ) )
				options->local_network = false;
			else if ( base::same_text ( arguments [ i ], L"--ask" ) )
				options->ask_internet = true;
			else if ( base::same_text ( arguments [ i ], L"--record" ) )
				record_path = next ( );
			else if ( base::same_text ( arguments [ i ], L"--record-every" ) )
				record_every = number ( next ( ) ) / 1000.0;
			else if ( base::same_text ( arguments [ i ], L"--record-speed" ) )
				record_speed = number ( next ( ) );
			else if ( base::same_text ( arguments [ i ], L"--record-from" ) )
				record_from = number ( next ( ) );
			else if ( base::same_text ( arguments [ i ], L"--record-size" ) ) {

				const wchar_t* value = next ( );
				int32_t across = 0;
				int32_t down = 0;
				int32_t c = 0;

				for ( ; value [ c ] >= L'0' && value [ c ] <= L'9'; c++ )
					across = across * 10 + ( value [ c ] - L'0' );

				for ( c += value [ c ] ? 1 : 0; value [ c ] >= L'0' && value [ c ] <= L'9'; c++ )
					down = down * 10 + ( value [ c ] - L'0' );

				if ( across >= 64 && down >= 48 ) {

					record_width = across;
					record_height = down;

				}

			}
			else if ( base::same_text ( arguments [ i ], L"--proxy" ) ) {

				const wchar_t* value = next ( );

				for ( int32_t c = 0; c < 127 && value [ c ]; c++ )
					options->proxy [ c ] = static_cast < char > ( value [ c ] );

			} else if ( base::same_text ( arguments [ i ], L"--screen" ) ) {

				const wchar_t* value = next ( );
				int32_t across = 0;
				int32_t down = 0;
				int32_t c = 0;

				for ( ; value [ c ] >= L'0' && value [ c ] <= L'9'; c++ )
					across = across * 10 + ( value [ c ] - L'0' );

				for ( c += value [ c ] ? 1 : 0; value [ c ] >= L'0' && value [ c ] <= L'9'; c++ )
					down = down * 10 + ( value [ c ] - L'0' );

				if ( across >= 640 && down >= 400 ) {

					options->screen_width = static_cast < uint16_t > ( across );
					options->screen_height = static_cast < uint16_t > ( down );

				}

			}
			else if ( base::same_text ( arguments [ i ], L"--unattend" ) )
				unattend_user = next ( );
			else if ( base::same_text ( arguments [ i ], L"--password" ) )
				unattend_password = next ( );
			else if ( base::same_text ( arguments [ i ], L"--variables" ) ) {

				const wchar_t* path = next ( );

				for ( int32_t c = 0; c < MAX_PATH - 1 && path [ c ]; c++ )
					options->variables [ c ] = path [ c ];

			}
			else if ( base::same_text ( arguments [ i ], L"--control" ) )
				control = next ( );
			else if ( base::same_text ( arguments [ i ], L"--create-disk" ) ) {

				uint64_t gigabytes = static_cast < uint64_t > ( number ( next ( ) ) );

				if ( options->disk [ 0 ] && GetFileAttributesW ( options->disk ) == INVALID_FILE_ATTRIBUTES )
					vm::create_disk ( options->disk, gigabytes << 30 );

			}
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

		if ( unattend_user ) {

			vm::setup_answers* answers = new vm::setup_answers ( );
			char error [ 160 ] = { };

			for ( int32_t c = 0; unattend_user [ c ] && c < 63; c++ )
				answers->user [ c ] = unattend_user [ c ];

			for ( int32_t c = 0; unattend_password [ c ] && c < 63; c++ )
				answers->password [ c ] = unattend_password [ c ];

			answers->uefi = options->uefi;
			vm::image_language ( options->iso, answers->language, sizeof ( answers->language ) );

			bool written = vm::write_setup_disc ( folder, *answers, options->helper, error, sizeof ( error ) );
			char note [ 200 ] = "answers: ";

			append ( note, sizeof ( note ), written ? "written, language " : error );
			append ( note, sizeof ( note ), written ? ( answers->language [ 0 ] ? answers->language : "unknown" ) : "" );
			append ( note, sizeof ( note ), "\r\n" );
			print ( note );
			delete answers;

		}

		pc::computer* machine = new pc::computer ( );
		counting_speaker* speaker = new counting_speaker ( );

		machine->speaker = speaker;

		{

			base::writer inbox;

			inbox.add ( folder ).add ( L"\\inbox" );
			CreateDirectoryW ( inbox.text ( ), nullptr );
			machine->tools.set_inbox ( inbox.text ( ) );

		}

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
			delete speaker;
			delete options;

			return 1;

		}

		double started = now_seconds ( );
		double next_shot = every;
		int32_t shot = 0;
		bool typed_done = typed == nullptr;
		int32_t resets = 0;
		gif::writer* recorder = nullptr;
		double next_record = record_from;

		if ( record_path ) {

			recorder = new gif::writer ( );

			if ( !recorder->open ( record_path, record_width, record_height ) ) {

				print ( "record: couldn't open the file\r\n" );
				delete recorder;
				recorder = nullptr;

			}

		}

		while ( now_seconds ( ) - started < seconds ) {

			Sleep ( recorder && record_every < 0.05 ? 10 : 50 );

			double elapsed = now_seconds ( ) - started;
			pc::computer_event event = machine->poll ( );

			if ( recorder && elapsed >= next_record ) {

				int32_t width = 0;
				int32_t height = 0;

				if ( machine->screen ( pixels, width, height, now_seconds ( ) ) && width > 0 && height > 0 ) {

					bool legacy = height == 200 || ( height == 400 && ( width == 720 || width == 640 || width == 360 ) );
					uint32_t delay = static_cast < uint32_t > ( record_every * 100.0 / ( record_speed > 0.0 ? record_speed : 1.0 ) + 0.5 );

					recorder->frame ( pixels, width, height, delay, legacy );

				}

				next_record += record_every;

				if ( next_record < elapsed )
					next_record = elapsed + record_every;

			}

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

			if ( control && !run_control ( *machine, control, folder, pixels ) )
				break;

			net::nat::access_request request;

			while ( machine->router && machine->router->next_request ( request ) ) {

				char note [ 400 ] = "ask: ";
				char digits [ 16 ];

				wsprintfA ( digits, "%u ", request.id );
				append ( note, sizeof ( note ), digits );
				append ( note, sizeof ( note ), request.host [ 0 ] ? request.host : "(address)" );
				wsprintfA ( digits, ":%u", request.remote_port );
				append ( note, sizeof ( note ), digits );
				append ( note, sizeof ( note ), request.protocol == 17 ? " udp\r\n" : " tcp\r\n" );
				print ( note );
				machine->tools.ask_owner ( request.id, request.protocol, request.guest_port );

				if ( trusting )
					machine->router->decide ( request.id, true );
				else if ( waiting_count < 2048 )
					waiting_ids [ waiting_count++ ] = request.id;

			}

			pc::tools_event happened;

			while ( machine->tools.next_event ( happened ) ) {

				char note [ 600 ] = "tools: ";
				static const char* const kinds [ 8 ] = { "none", "connected", "disconnected", "text ", "files ", "sent", "refused", "owner " };

				append ( note, sizeof ( note ), kinds [ happened.kind < 8 ? happened.kind : 0 ] );

				if ( happened.kind == pc::TOOLS_OWNER_FOUND ) {

					char digits [ 16 ];

					wsprintfA ( digits, "%u ", happened.count );
					append ( note, sizeof ( note ), digits );

				}

				const base::list < wchar_t >& words = happened.kind == pc::TOOLS_GOT_TEXT || happened.kind == pc::TOOLS_OWNER_FOUND ? happened.text : happened.items;
				char narrow [ 400 ] = { };

				if ( !words.empty ( ) )
					WideCharToMultiByte ( CP_UTF8, 0, words.data ( ), -1, narrow, sizeof ( narrow ) - 1, nullptr, nullptr );

				append ( note, sizeof ( note ), narrow );
				append ( note, sizeof ( note ), "\r\n" );
				print ( note );

			}

			if ( every > 0.0 && elapsed >= next_shot ) {

				base::writer name;

				name.add ( L"shot_" ).number ( shot++ ).add ( L".png" );
				save_screen ( *machine, folder, name.text ( ), pixels );
				next_shot += every;

			}

		}

		save_screen ( *machine, folder, L"final.png", pixels );

		if ( recorder ) {

			char note [ 96 ];
			int32_t frames = recorder->frames;
			bool closed = recorder->close ( );

			wsprintfA ( note, "record: %d frames%s\r\n", frames, closed ? "" : ", but the file failed" );
			print ( note );
			delete recorder;

		}

		static char console [ 65536 ];

		machine->console_text ( console, sizeof ( console ) );
		line [ 0 ] = 0;
		append ( line, sizeof ( line ), "boot: " );
		append ( line, sizeof ( line ), machine->uefi_firmware ? machine->uefi_firmware->last_boot : ( machine->firmware ? machine->firmware->last_boot : "-" ) );

		if ( machine->uefi_firmware ) {

			append ( line, sizeof ( line ), "\r\nuefi calls " );
			append_number ( line, sizeof ( line ), machine->uefi_firmware->calls );
			append ( line, sizeof ( line ), " " );
			append ( line, sizeof ( line ), machine->uefi_firmware->trouble );

		}
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

		if ( machine->storage ) {

			static char traced [ 8192 ];

			traced [ 0 ] = 0;
			append ( traced, sizeof ( traced ), machine->drive.present ( ) ? "disk open\r\nide:" : "disk missing\r\nide:" );

			for ( uint32_t i = 0; i < machine->storage->trace_count; i++ ) {

				append ( traced, sizeof ( traced ), " " );
				append_hex ( traced, sizeof ( traced ), machine->storage->trace [ i ], 4 );

			}

			append ( traced, sizeof ( traced ), "\r\nnic sent " );
			append_number ( traced, sizeof ( traced ), machine->nic ? machine->nic->frames_sent : -1 );
			append ( traced, sizeof ( traced ), ", received " );
			append_number ( traced, sizeof ( traced ), machine->nic ? machine->nic->frames_received : -1 );
			append ( traced, sizeof ( traced ), ", tablet polls " );
			append_number ( traced, sizeof ( traced ), machine->tablet ? machine->tablet->polls : -1 );
			append ( traced, sizeof ( traced ), ", usb frames " );
			append_number ( traced, sizeof ( traced ), machine->usb ? machine->usb->frames_run : -1 );
			append ( traced, sizeof ( traced ), "\r\nsound verbs " );
			append_number ( traced, sizeof ( traced ), machine->sound ? machine->sound->verbs_run : -1 );
			append ( traced, sizeof ( traced ), ", frames " );
			append_number ( traced, sizeof ( traced ), speaker->frames );
			append ( traced, sizeof ( traced ), ", peak x1000 " );
			append_number ( traced, sizeof ( traced ), static_cast < int64_t > ( speaker->peak * 1000.0f ) );
			append ( traced, sizeof ( traced ), "\r\nnat in " );
			append_number ( traced, sizeof ( traced ), machine->router ? machine->router->frames_in : -1 );
			append ( traced, sizeof ( traced ), ", out " );
			append_number ( traced, sizeof ( traced ), machine->router ? machine->router->frames_out : -1 );
			append ( traced, sizeof ( traced ), "\r\n" );
			print ( traced );

		}
		print ( console );
		print ( "\r\n" );
		machine->power_off ( );
		runtime_release ( pixels );
		delete machine;
		delete speaker;
		delete options;

		return 0;

	}

}

#endif