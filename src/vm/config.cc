#include "vm/config.hh"
#include "base/files.hh"
#include <winioctl.h>

namespace vm {

	static const preset preset_table [ ] = {

		{ "windows11", L"Windows 11", L"64-bit, 4 GB memory, 64 GB disk", L"W", MACHINE_PC, 4096, 2, 64 },
		{ "windows10", L"Windows 10", L"64-bit, 4 GB memory, 50 GB disk", L"W", MACHINE_PC, 4096, 2, 50 },
		{ "ubuntu", L"Ubuntu", L"64-bit, 4 GB memory, 25 GB disk", L"U", MACHINE_PC, 4096, 2, 25 },
		{ "debian", L"Debian", L"64-bit, 2 GB memory, 20 GB disk", L"D", MACHINE_PC, 2048, 2, 20 },
		{ "fedora", L"Fedora", L"64-bit, 4 GB memory, 25 GB disk", L"F", MACHINE_PC, 4096, 2, 25 },
		{ "linux", L"Other Linux", L"64-bit, 2 GB memory, 16 GB disk", L"L", MACHINE_PC, 2048, 2, 16 },
		{ "pc", L"Other PC", L"1 GB memory, 16 GB disk", L"PC", MACHINE_PC, 1024, 1, 16 },
		{ "pcxt", L"IBM PC/XT", L"8088 at 4.77 MHz, CGA, two floppy drives", L"XT", MACHINE_XT, 640, 1, 0 },
		{ "freedos", L"FreeDOS on an XT", L"For the FreeDOS 1.3 floppy edition", L"FD", MACHINE_XT, 640, 1, 0 },

	};

	static bool contains_text ( const wchar_t* text, const wchar_t* part ) {

		for ( int32_t i = 0; text [ i ]; i++ ) {

			int32_t j = 0;

			while ( part [ j ] && text [ i + j ] && ( text [ i + j ] | 0x20 ) == ( part [ j ] | 0x20 ) )
				j++;

			if ( !part [ j ] )
				return true;

		}

		return false;

	}

	const preset* detect_image ( const wchar_t* path ) {

		wchar_t label [ 40 ] = { };
		uint8_t sector [ 2048 ];
		HANDLE file = CreateFileW ( path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
		LARGE_INTEGER at;
		DWORD read = 0;

		at.QuadPart = 0x8000;

		if ( file != INVALID_HANDLE_VALUE ) {

			if ( SetFilePointerEx ( file, at, nullptr, FILE_BEGIN ) && ReadFile ( file, sector, sizeof ( sector ), &read, nullptr ) && read == sizeof ( sector ) && sector [ 1 ] == 'C' && sector [ 2 ] == 'D' && sector [ 3 ] == '0' && sector [ 4 ] == '0' && sector [ 5 ] == '1' ) {

				for ( int32_t i = 0; i < 32; i++ )
					label [ i ] = sector [ 40 + i ];

			}

			CloseHandle ( file );

		}

		const wchar_t* name = path;

		for ( const wchar_t* c = path; *c; c++ ) {

			if ( *c == L'\\' || *c == L'/' )
				name = c + 1;

		}

		auto either = [ & ] ( const wchar_t* part ) {

			return contains_text ( label, part ) || contains_text ( name, part );

		};

		if ( either ( L"win10" ) || either ( L"windows 10" ) || either ( L"windows10" ) )
			return find_preset ( "windows10" );

		if ( either ( L"win11" ) || either ( L"windows" ) || either ( L"CCCOMA" ) || either ( L"CPBA" ) || either ( L"CCSA" ) || either ( L"CENA" ) || either ( L"SSS_X64" ) )
			return find_preset ( "windows11" );

		if ( either ( L"ubuntu" ) )
			return find_preset ( "ubuntu" );

		if ( either ( L"debian" ) )
			return find_preset ( "debian" );

		if ( either ( L"fedora" ) )
			return find_preset ( "fedora" );

		if ( either ( L"linux" ) || either ( L"arch" ) || either ( L"mint" ) || either ( L"opensuse" ) || either ( L"manjaro" ) || either ( L"kali" ) || either ( L"alpine" ) || either ( L"pop_os" ) )
			return find_preset ( "linux" );

		return find_preset ( "pc" );

	}

	host_info read_host ( ) {

		host_info info;
		MEMORYSTATUSEX memory = { sizeof ( MEMORYSTATUSEX ) };
		SYSTEM_INFO system;
		uint8_t buffer [ 8192 ];
		DWORD size = sizeof ( buffer );

		if ( GlobalMemoryStatusEx ( &memory ) )
			info.memory_mb = static_cast < uint32_t > ( memory.ullTotalPhys / ( 1024 * 1024 ) );

		GetSystemInfo ( &system );
		info.logical = system.dwNumberOfProcessors ? system.dwNumberOfProcessors : 1;
		info.physical = info.logical;

		if ( GetLogicalProcessorInformationEx ( RelationProcessorCore, reinterpret_cast < SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* > ( buffer ), &size ) ) {

			uint32_t cores = 0;

			for ( DWORD offset = 0; offset < size; ) {

				const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* item = reinterpret_cast < const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* > ( buffer + offset );

				cores++;
				offset += item->Size;

			}

			if ( cores )
				info.physical = cores;

		}

		return info;

	}

	bool create_disk ( const wchar_t* path, uint64_t bytes ) {

		HANDLE file = CreateFileW ( path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr );
		DWORD returned = 0;
		LARGE_INTEGER end;

		if ( file == INVALID_HANDLE_VALUE )
			return false;

		end.QuadPart = static_cast < LONGLONG > ( bytes );

		bool made = DeviceIoControl ( file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr ) && SetFilePointerEx ( file, end, nullptr, FILE_BEGIN ) && SetEndOfFile ( file );

		CloseHandle ( file );

		if ( !made )
			DeleteFileW ( path );

		return made;

	}

	const preset* presets ( int32_t& count ) {

		count = static_cast < int32_t > ( sizeof ( preset_table ) / sizeof ( preset_table [ 0 ] ) );

		return preset_table;

	}

	const preset* find_preset ( const char* id ) {

		for ( const preset& item : preset_table ) {

			if ( base::same_text ( item.id, id ) )
				return &item;

		}

		return nullptr;

	}

	static bool fail ( char* out, int32_t room, int32_t line, const char* text ) {

		if ( !out || room <= 0 )
			return false;

		int32_t used = 0;

		auto add = [ & ] ( const char* part ) {

			for ( int32_t i = 0; part [ i ] && used < room - 1; i++ )
				out [ used++ ] = part [ i ];

		};

		if ( line > 0 ) {

			char digits [ 12 ];
			int32_t count = 0;

			add ( "line " );

			do {

				digits [ count++ ] = static_cast < char > ( '0' + line % 10 );
				line /= 10;

			} while ( line );

			while ( count && used < room - 1 )
				out [ used++ ] = digits [ --count ];

			add ( ": " );

		}

		add ( text );
		out [ used ] = 0;

		return false;

	}

	static bool same_key ( const char* line, int32_t size, const char* key ) {

		int32_t length = base::text_length ( key );

		if ( length != size )
			return false;

		for ( int32_t i = 0; i < size; i++ ) {

			if ( line [ i ] != key [ i ] )
				return false;

		}

		return true;

	}

	static bool number ( const char* text, int32_t size, int64_t& out ) {

		bool negative = size > 0 && text [ 0 ] == '-';
		int32_t at = negative ? 1 : 0;

		if ( at >= size || size - at > 10 )
			return false;

		int64_t value = 0;

		for ( ; at < size; at++ ) {

			if ( text [ at ] < '0' || text [ at ] > '9' )
				return false;

			value = value * 10 + ( text [ at ] - '0' );

		}

		out = negative ? -value : value;

		return true;

	}

	static bool wide ( const char* text, int32_t size, wchar_t* out, int32_t room ) {

		if ( !size ) {

			out [ 0 ] = 0;

			return true;

		}

		int32_t written = MultiByteToWideChar ( CP_UTF8, MB_ERR_INVALID_CHARS, text, size, out, room - 1 );

		if ( written <= 0 )
			return false;

		out [ written ] = 0;

		return true;

	}

	static bool reserved ( const wchar_t* name ) {

		static const wchar_t* const devices [ ] = { L"CON", L"PRN", L"AUX", L"NUL" };
		wchar_t upper [ 8 ] = { };
		int32_t length = 0;

		for ( ; name [ length ] && name [ length ] != L'.' && length < 7; length++ )
			upper [ length ] = name [ length ] >= L'a' && name [ length ] <= L'z' ? static_cast < wchar_t > ( name [ length ] - 32 ) : name [ length ];

		if ( name [ length ] && name [ length ] != L'.' )
			return false;

		for ( const wchar_t* device : devices ) {

			if ( base::same_text ( upper, device ) )
				return true;

		}

		return length == 4 && ( ( upper [ 0 ] == L'C' && upper [ 1 ] == L'O' && upper [ 2 ] == L'M' ) || ( upper [ 0 ] == L'L' && upper [ 1 ] == L'P' && upper [ 2 ] == L'T' ) ) && upper [ 3 ] >= L'1' && upper [ 3 ] <= L'9';

	}

	const screen_size* screen_choices ( ) {

		static const screen_size table [ screen_choice_count ] = {

			{ 0, 0 }, { 1024, 768 }, { 1280, 720 }, { 1280, 800 },
			{ 1366, 768 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 },

		};

		return table;

	}

	const uint16_t* frame_choices ( ) {

		static const uint16_t table [ frame_choice_count ] = { 0, 30, 60, 90, 120, 144 };

		return table;

	}

	bool valid_proxy ( const char* text, int32_t length ) {

		int32_t colon = -1;

		for ( int32_t i = 0; i < length; i++ ) {

			if ( text [ i ] == ':' )
				colon = i;

		}

		if ( colon < 1 || colon == length - 1 || length >= 128 )
			return false;

		for ( int32_t i = 0; i < colon; i++ ) {

			char c = text [ i ];

			if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '.' || c == '-' ) )
				return false;

		}

		int64_t port = 0;

		for ( int32_t i = colon + 1; i < length; i++ ) {

			if ( text [ i ] < '0' || text [ i ] > '9' || length - colon > 6 )
				return false;

			port = port * 10 + ( text [ i ] - '0' );

		}

		return port >= 1 && port <= 65535;

	}

	bool read_launch ( const wchar_t* const* arguments, int32_t count, launch_choice& out ) {

		bool any = false;

		for ( int32_t i = 1; arguments && i + 1 < count; i++ ) {

			const wchar_t* value = arguments [ i + 1 ];
			int8_t yes = base::same_text ( value, L"1" ) || base::same_text ( value, L"fresh" ) || base::same_text ( value, L"proxy" ) || base::same_text ( value, L"ask" ) ? 1 : 0;

			if ( base::same_text ( arguments [ i ], L"--disposable" ) )
				out.disposable = yes;
			else if ( base::same_text ( arguments [ i ], L"--identity" ) )
				out.fresh_identity = yes;
			else if ( base::same_text ( arguments [ i ], L"--local-network" ) )
				out.local_network = yes;
			else if ( base::same_text ( arguments [ i ], L"--route" ) )
				out.use_proxy = yes;
			else if ( base::same_text ( arguments [ i ], L"--internet" ) )
				out.ask_internet = yes;
			else
				continue;

			any = true;
			i++;

		}

		return any;

	}

	void apply_launch ( const launch_choice& choice, config& run ) {

		if ( choice.disposable >= 0 )
			run.disposable = choice.disposable != 0;

		if ( choice.fresh_identity >= 0 )
			run.fresh_identity = choice.fresh_identity != 0;

		if ( choice.local_network >= 0 )
			run.local_network = choice.local_network != 0;

		if ( choice.use_proxy >= 0 )
			run.use_proxy = choice.use_proxy != 0 && run.proxy [ 0 ];

		if ( choice.ask_internet >= 0 )
			run.ask_internet = choice.ask_internet != 0;

	}

	bool private_run ( const config& run ) {

		return run.disposable || run.fresh_identity || !run.local_network || ( run.use_proxy && run.proxy [ 0 ] ) || !run.keep_preview || run.ask_internet;

	}

	bool valid_name ( const wchar_t* name ) {

		int32_t length = base::text_length ( name );
		bool visible = false;

		if ( length < 1 || length >= name_room || name [ length - 1 ] == L' ' || name [ length - 1 ] == L'.' )
			return false;

		for ( int32_t i = 0; i < length; i++ ) {

			wchar_t c = name [ i ];

			if ( c < 32 || c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|' )
				return false;

			visible = visible || c != L' ';

		}

		return visible && !reserved ( name );

	}

	bool parse ( const char* text, int32_t length, config& out, char* error, int32_t room ) {

		config* result = new config ( );
		bool versioned = false;
		bool named = false;
		int32_t line_number = 0;
		int32_t start = 0;
		bool ok = true;

		if ( length >= 3 && static_cast < uint8_t > ( text [ 0 ] ) == 0xEF && static_cast < uint8_t > ( text [ 1 ] ) == 0xBB && static_cast < uint8_t > ( text [ 2 ] ) == 0xBF )
			start = 3;

		while ( ok && start < length ) {

			int32_t end = start;

			while ( end < length && text [ end ] != '\n' )
				end++;

			int32_t size = end - start;
			const char* line = text + start;

			line_number++;
			start = end + 1;

			if ( size && line [ size - 1 ] == '\r' )
				size--;

			if ( !size )
				continue;

			int32_t equals = 0;

			while ( equals < size && line [ equals ] != '=' )
				equals++;

			if ( !equals || equals == size ) {

				ok = fail ( error, room, line_number, "expected key=value" );

				break;

			}

			const char* value = line + equals + 1;
			int32_t value_size = size - equals - 1;
			int64_t amount = 0;

			auto key = [ & ] ( const char* name ) {

				return same_key ( line, equals, name );

			};

			auto flag = [ & ] ( bool& target ) {

				if ( value_size != 1 || ( value [ 0 ] != '0' && value [ 0 ] != '1' ) )
					return fail ( error, room, line_number, "expected 0 or 1" );

				target = value [ 0 ] == '1';

				return true;

			};

			auto path = [ & ] ( wchar_t* target ) {

				return wide ( value, value_size, target, MAX_PATH ) || fail ( error, room, line_number, "the path is too long or isn't valid UTF-8" );

			};

			if ( !versioned ) {

				if ( !key ( "coldboot" ) )
					ok = fail ( error, room, line_number, "this isn't a Coldboot machine file" );
				else if ( !number ( value, value_size, amount ) || amount < 1 )
					ok = fail ( error, room, line_number, "the version line is broken" );
				else if ( amount > config_version )
					ok = fail ( error, room, line_number, "this machine was made by a newer Coldboot" );

				versioned = true;

				continue;

			}

			if ( key ( "name" ) ) {

				if ( !wide ( value, value_size, result->name, name_room ) || !valid_name ( result->name ) )
					ok = fail ( error, room, line_number, "the name is empty or has characters Windows can't use in a folder name" );

				named = true;

			} else if ( key ( "os" ) ) {

				bool clean = value_size > 0 && value_size < static_cast < int32_t > ( sizeof ( result->os ) );

				for ( int32_t i = 0; clean && i < value_size; i++ )
					clean = ( value [ i ] >= 'a' && value [ i ] <= 'z' ) || ( value [ i ] >= '0' && value [ i ] <= '9' ) || value [ i ] == '-';

				if ( !clean ) {

					ok = fail ( error, room, line_number, "the OS id isn't valid" );

				} else {

					for ( int32_t i = 0; i < value_size; i++ )
						result->os [ i ] = value [ i ];

					result->os [ value_size ] = 0;

				}

			} else if ( key ( "machine" ) ) {

				if ( same_key ( value, value_size, "xt" ) )
					result->machine = MACHINE_XT;
				else if ( same_key ( value, value_size, "pc" ) )
					result->machine = MACHINE_PC;
				else
					ok = fail ( error, room, line_number, "Coldboot can't run this machine type yet" );

			} else if ( key ( "memory_mb" ) ) {

				if ( !number ( value, value_size, amount ) || amount < 16 || amount > 1048576 )
					ok = fail ( error, room, line_number, "memory has to be 16 MB to 1 TB" );
				else
					result->memory_mb = static_cast < uint32_t > ( amount );

			} else if ( key ( "processors" ) ) {

				if ( !number ( value, value_size, amount ) || amount < 1 || amount > 64 )
					ok = fail ( error, room, line_number, "processors have to be 1 to 64" );
				else
					result->processors = static_cast < uint32_t > ( amount );

			} else if ( key ( "cap" ) ) {

				if ( !number ( value, value_size, amount ) || amount < 5 || amount > 100 )
					ok = fail ( error, room, line_number, "the CPU cap has to be 5 to 100 percent" );
				else
					result->cap = static_cast < uint32_t > ( amount );

			} else if ( key ( "boot" ) ) {

				if ( same_key ( value, value_size, "disk" ) )
					result->disk_first = true;
				else if ( same_key ( value, value_size, "cd" ) )
					result->disk_first = false;
				else
					ok = fail ( error, room, line_number, "boot has to be cd or disk" );

			} else if ( key ( "firmware" ) ) {

				if ( same_key ( value, value_size, "uefi" ) )
					result->uefi = true;
				else if ( same_key ( value, value_size, "bios" ) )
					result->uefi = false;
				else
					ok = fail ( error, room, line_number, "firmware has to be bios or uefi" );

			} else if ( key ( "clipboard" ) || key ( "file_sharing" ) ) {

				uint8_t& target = key ( "clipboard" ) ? result->clipboard : result->file_sharing;

				if ( same_key ( value, value_size, "off" ) )
					target = 0;
				else if ( same_key ( value, value_size, "to-machine" ) )
					target = 1;
				else if ( same_key ( value, value_size, "from-machine" ) )
					target = 2;
				else if ( same_key ( value, value_size, "both" ) )
					target = 3;
				else
					ok = fail ( error, room, line_number, "sharing has to be off, to-machine, from-machine or both" );

			} else if ( key ( "screen" ) ) {

				int32_t cross = 0;
				int64_t across = 0;

				while ( cross < value_size && value [ cross ] != 'x' )
					cross++;

				if ( same_key ( value, value_size, "auto" ) ) {

					result->screen_width = 0;
					result->screen_height = 0;

				} else if ( cross >= value_size || !number ( value, cross, across ) || !number ( value + cross + 1, value_size - cross - 1, amount ) || across < 640 || across > 2560 || amount < 400 || amount > 1600 ) {

					ok = fail ( error, room, line_number, "the screen has to be from 640x400 to 2560x1600" );

				} else {

					result->screen_width = static_cast < uint16_t > ( across );
					result->screen_height = static_cast < uint16_t > ( amount );

				}

			} else if ( key ( "frame_rate" ) ) {

				if ( same_key ( value, value_size, "display" ) )
					result->frame_rate = 0;
				else if ( !number ( value, value_size, amount ) || amount < 15 || amount > 360 )
					ok = fail ( error, room, line_number, "the frame rate has to be display or 15 to 360" );
				else
					result->frame_rate = static_cast < uint16_t > ( amount );

			} else if ( key ( "scaling" ) ) {

				if ( same_key ( value, value_size, "fit" ) )
					result->scaling = 0;
				else if ( same_key ( value, value_size, "whole" ) )
					result->scaling = 1;
				else if ( same_key ( value, value_size, "actual" ) )
					result->scaling = 2;
				else
					ok = fail ( error, room, line_number, "scaling has to be fit, whole or actual" );

			} else if ( key ( "filter" ) ) {

				if ( same_key ( value, value_size, "sharp" ) )
					result->smooth = false;
				else if ( same_key ( value, value_size, "smooth" ) )
					result->smooth = true;
				else
					ok = fail ( error, room, line_number, "the filter has to be sharp or smooth" );

			} else if ( key ( "identity" ) ) {

				if ( same_key ( value, value_size, "stable" ) )
					result->fresh_identity = false;
				else if ( same_key ( value, value_size, "fresh" ) )
					result->fresh_identity = true;
				else
					ok = fail ( error, room, line_number, "identity has to be stable or fresh" );

			} else if ( key ( "proxy" ) ) {

				if ( value_size && !valid_proxy ( value, value_size ) ) {

					ok = fail ( error, room, line_number, "the proxy has to be host:port" );

				} else {

					for ( int32_t i = 0; i < value_size; i++ )
						result->proxy [ i ] = value [ i ];

					result->proxy [ value_size ] = 0;

				}

			} else if ( key ( "route" ) ) {

				if ( same_key ( value, value_size, "direct" ) )
					result->use_proxy = false;
				else if ( same_key ( value, value_size, "proxy" ) )
					result->use_proxy = true;
				else
					ok = fail ( error, room, line_number, "the route has to be direct or proxy" );

			} else if ( key ( "internet" ) ) {

				if ( same_key ( value, value_size, "open" ) )
					result->ask_internet = false;
				else if ( same_key ( value, value_size, "ask" ) )
					result->ask_internet = true;
				else
					ok = fail ( error, room, line_number, "internet has to be open or ask" );

			} else if ( key ( "frame_counter" ) )
				ok = flag ( result->frame_counter );
			else if ( key ( "local_network" ) )
				ok = flag ( result->local_network );
			else if ( key ( "disposable" ) )
				ok = flag ( result->disposable );
			else if ( key ( "preview" ) )
				ok = flag ( result->keep_preview );
			else if ( key ( "network" ) )
				ok = flag ( result->network );
			else if ( key ( "unattended" ) )
				ok = flag ( result->unattended );
			else if ( key ( "iso" ) )
				ok = path ( result->iso );
			else if ( key ( "disk" ) )
				ok = path ( result->disk );
			else if ( key ( "memory" ) ) {

				if ( !number ( value, value_size, amount ) || amount < 64 || amount > 640 || amount % 16 )
					ok = fail ( error, room, line_number, "memory has to be 64 to 640 KB in steps of 16" );
				else
					result->memory = static_cast < uint32_t > ( amount );

			} else if ( key ( "turbo" ) )
				ok = flag ( result->turbo );
			else if ( key ( "crt" ) )
				ok = flag ( result->crt );
			else if ( key ( "fast_floppy" ) )
				ok = flag ( result->fast_floppy );
			else if ( key ( "custom_bios" ) )
				ok = flag ( result->custom_bios );
			else if ( key ( "maximized" ) )
				ok = flag ( result->maximized );
			else if ( key ( "window_x" ) || key ( "window_y" ) || key ( "window_width" ) || key ( "window_height" ) ) {

				if ( !number ( value, value_size, amount ) || amount < -100000 || amount > 100000 ) {

					ok = fail ( error, room, line_number, "the window position isn't a number" );

				} else {

					int32_t place = static_cast < int32_t > ( amount );

					if ( key ( "window_x" ) )
						result->window_x = place;
					else if ( key ( "window_y" ) )
						result->window_y = place;
					else if ( key ( "window_width" ) )
						result->window_width = place;
					else
						result->window_height = place;

				}

			} else if ( key ( "bios" ) )
				ok = path ( result->bios );
			else if ( key ( "floppy_a" ) )
				ok = path ( result->floppies [ 0 ] );
			else if ( key ( "floppy_b" ) )
				ok = path ( result->floppies [ 1 ] );
			else if ( key ( "notes" ) ) {

				wchar_t* raw = new wchar_t [ notes_room ];

				if ( !wide ( value, value_size, raw, notes_room ) ) {

					ok = fail ( error, room, line_number, "the notes are too long or aren't valid UTF-8" );

				} else {

					int32_t used = 0;

					for ( int32_t i = 0; raw [ i ] && used < notes_room - 1; i++ ) {

						wchar_t c = raw [ i ];

						if ( c == L'\\' && raw [ i + 1 ] ) {

							i++;
							c = raw [ i ] == L'n' ? L'\n' : raw [ i ];

						}

						result->notes [ used++ ] = c;

					}

					result->notes [ used ] = 0;

				}

				delete [ ] raw;

			}

		}

		if ( ok && !versioned )
			ok = fail ( error, room, 0, "the file is empty" );

		if ( ok && !named )
			ok = fail ( error, room, 0, "the machine has no name" );

		if ( ok )
			out = *result;

		delete result;

		return ok;

	}

	static void put ( base::list < char >& out, const char* text ) {

		for ( int32_t i = 0; text [ i ]; i++ )
			out.push ( text [ i ] );

	}

	static void put_number ( base::list < char >& out, const char* key, int64_t value ) {

		char digits [ 24 ];
		int32_t count = 0;
		bool negative = value < 0;
		uint64_t magnitude = negative ? static_cast < uint64_t > ( -value ) : static_cast < uint64_t > ( value );

		do {

			digits [ count++ ] = static_cast < char > ( '0' + magnitude % 10 );
			magnitude /= 10;

		} while ( magnitude );

		put ( out, key );
		out.push ( '=' );

		if ( negative )
			out.push ( '-' );

		while ( count )
			out.push ( digits [ --count ] );

		out.push ( '\n' );

	}

	static void put_text ( base::list < char >& out, const char* key, const wchar_t* text, bool escape ) {

		put ( out, key );
		out.push ( '=' );

		for ( int32_t i = 0; text [ i ]; i++ ) {

			wchar_t c = text [ i ];
			char bytes [ 8 ];

			if ( escape && ( c == L'\\' || c == L'\n' ) ) {

				out.push ( '\\' );
				out.push ( c == L'\n' ? 'n' : '\\' );

				continue;

			}

			if ( c == L'\r' || ( c == L'\n' && !escape ) )
				continue;

			int32_t pair = c >= 0xD800 && c <= 0xDBFF && text [ i + 1 ] ? 2 : 1;
			int32_t size = WideCharToMultiByte ( CP_UTF8, 0, text + i, pair, bytes, sizeof ( bytes ), nullptr, nullptr );

			for ( int32_t b = 0; b < size; b++ )
				out.push ( bytes [ b ] );

			i += pair - 1;

		}

		out.push ( '\n' );

	}

	void serialize ( const config& in, base::list < char >& out ) {

		put_number ( out, "coldboot", config_version );
		put_text ( out, "name", in.name, false );
		put ( out, "os=" );
		put ( out, in.os );

		if ( in.machine == MACHINE_PC ) {

			put ( out, "\nmachine=pc\n" );
			put_number ( out, "memory_mb", in.memory_mb );
			put_number ( out, "processors", in.processors );
			put_number ( out, "cap", in.cap );
			put ( out, in.disk_first ? "boot=disk\n" : "boot=cd\n" );
			put ( out, in.uefi ? "firmware=uefi\n" : "firmware=bios\n" );
			put_number ( out, "network", in.network );

			static const char* const shares [ 4 ] = { "off\n", "to-machine\n", "from-machine\n", "both\n" };

			put ( out, "clipboard=" );
			put ( out, shares [ in.clipboard & 3 ] );
			put ( out, "file_sharing=" );
			put ( out, shares [ in.file_sharing & 3 ] );
			put_number ( out, "unattended", in.unattended );

			char screen [ 24 ];
			int32_t used = 0;

			auto digits = [ & ] ( uint32_t value ) {

				char reversed [ 8 ];
				int32_t count = 0;

				do {

					reversed [ count++ ] = static_cast < char > ( '0' + value % 10 );
					value /= 10;

				} while ( value );

				while ( count )
					screen [ used++ ] = reversed [ --count ];

			};

			digits ( in.screen_width );
			screen [ used++ ] = 'x';
			digits ( in.screen_height );
			screen [ used ] = 0;
			put ( out, "screen=" );
			put ( out, in.screen_width ? screen : "auto" );
			out.push ( '\n' );

			if ( in.frame_rate )
				put_number ( out, "frame_rate", in.frame_rate );
			else
				put ( out, "frame_rate=display\n" );

			static const char* const scalings [ 3 ] = { "scaling=fit\n", "scaling=whole\n", "scaling=actual\n" };

			put ( out, scalings [ in.scaling < 3 ? in.scaling : 0 ] );
			put ( out, in.smooth ? "filter=smooth\n" : "filter=sharp\n" );
			put_number ( out, "frame_counter", in.frame_counter );
			put_number ( out, "local_network", in.local_network );
			put ( out, "proxy=" );
			put ( out, in.proxy );
			out.push ( '\n' );
			put ( out, in.use_proxy ? "route=proxy\n" : "route=direct\n" );
			put ( out, in.ask_internet ? "internet=ask\n" : "internet=open\n" );
			put ( out, in.fresh_identity ? "identity=fresh\n" : "identity=stable\n" );
			put_number ( out, "disposable", in.disposable );
			put_number ( out, "preview", in.keep_preview );
			put_text ( out, "iso", in.iso, false );
			put_text ( out, "disk", in.disk, false );

		} else {

			put ( out, "\nmachine=xt\n" );
			put_number ( out, "memory", in.memory );
			put_number ( out, "turbo", in.turbo );
			put_number ( out, "crt", in.crt );
			put_number ( out, "fast_floppy", in.fast_floppy );
			put_number ( out, "custom_bios", in.custom_bios );
			put_text ( out, "bios", in.bios, false );
			put_text ( out, "floppy_a", in.floppies [ 0 ], false );
			put_text ( out, "floppy_b", in.floppies [ 1 ], false );

		}

		put_number ( out, "maximized", in.maximized );
		put_number ( out, "window_x", in.window_x );
		put_number ( out, "window_y", in.window_y );
		put_number ( out, "window_width", in.window_width );
		put_number ( out, "window_height", in.window_height );
		put_text ( out, "notes", in.notes, true );

	}

	bool load ( const wchar_t* path, config& out, char* error, int32_t room ) {

		base::list < char > data;

		if ( !files::read ( path, data ) )
			return fail ( error, room, 0, "the file can't be read" );

		return parse ( data.data ( ), data.count ( ) - 1, out, error, room );

	}

	bool save ( const wchar_t* path, const config& in ) {

		base::list < char > text;
		base::writer staging;

		serialize ( in, text );
		staging.add ( path ).add ( L".new" );

		if ( !files::write ( staging.text ( ), text.data ( ), text.count ( ) ) )
			return false;

		return MoveFileExW ( staging.text ( ), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) != 0;

	}

}