#include "app/session.hh"
#include "base/files.hh"
#include "storage/program.hh"

namespace app {

	static constexpr uint32_t state_header = 0x46534243;

	static void copy_path ( wchar_t* out, const wchar_t* path ) {

		int32_t length = 0;

		for ( ; path && path [ length ] && length < MAX_PATH - 1; length++ )
			out [ length ] = path [ length ];

		out [ length ] = 0;

	}

	static const wchar_t* file_name ( const wchar_t* path ) {

		const wchar_t* name = path;

		for ( const wchar_t* at = path; *at; at++ ) {

			if ( *at == L'\\' || *at == L'/' )
				name = at + 1;

		}

		return name;

	}

	static void set_problem ( wchar_t* out, const wchar_t* text ) {

		int32_t length = 0;

		for ( ; text [ length ] && length < 199; length++ )
			out [ length ] = text [ length ];

		out [ length ] = 0;

	}

	session::session ( ) {

		board = new machine::xt ( );
		shown = static_cast < uint32_t* > ( runtime_allocate_zero ( sizeof ( uint32_t ) * machine::cga::max_width * machine::cga::max_height ) );

	}

	session::~session ( ) {

		power_off ( );

		for ( int32_t i = 0; i < drive_count; i++ )
			save_disk ( i );

		delete board;
		runtime_release ( shown );

	}

	void session::lock_board ( ) {

		InterlockedIncrement ( &requests );
		AcquireSRWLockExclusive ( &board_lock );

	}

	void session::unlock_board ( ) {

		ReleaseSRWLockExclusive ( &board_lock );
		InterlockedDecrement ( &requests );

	}

	bool session::power_on ( HWND window, const settings& config ) {

		power_off ( );
		window_handle = window;
		problem [ 0 ] = 0;

		lock_board ( );

		board->set_memory ( config.memory );
		board->floppy.fast = config.fast_floppy;

		bool loaded = false;

		if ( config.custom_bios && config.bios [ 0 ] ) {

			base::list < char > image;

			loaded = files::read ( config.bios, image ) && image.count ( ) > 1 && board->load_bios ( reinterpret_cast < const uint8_t* > ( image.data ( ) ), static_cast < uint32_t > ( image.count ( ) - 1 ) );

			if ( !loaded )
				set_problem ( problem, L"That BIOS file can't be used. Coldboot takes a raw ROM image of 64 KB or less." );

		} else {

			char message [ 200 ] = { };

			loaded = board->load_builtin ( message, sizeof ( message ), nullptr );

			if ( !loaded ) {

				int32_t written = MultiByteToWideChar ( CP_UTF8, 0, message, -1, problem, 199 );

				problem [ written > 0 ? written - 1 : 0 ] = 0;

			}

		}

		if ( !loaded ) {

			unlock_board ( );
			state = SESSION_FAILED;

			return false;

		}

		board->power_on ( );
		unlock_board ( );

		if ( !muted && sound.start ( ) )
			board->sample_rate = sound.rate;

		key_count = 0;
		quit = 0;
		posted = 0;
		code = 0;
		paused = false;
		turbo = config.turbo;
		state = SESSION_RUNNING;
		thread = CreateThread ( nullptr, 0, &loop, this, 0, nullptr );

		return thread != nullptr;

	}

	void session::power_off ( ) {

		if ( thread ) {

			InterlockedExchange ( &quit, 1 );
			WaitForSingleObject ( thread, INFINITE );
			CloseHandle ( thread );
			thread = nullptr;

		}

		sound.stop ( );

		for ( int32_t i = 0; i < drive_count; i++ )
			save_disk ( i );

		InterlockedExchange ( &lights, 0 );
		AcquireSRWLockExclusive ( &frame_lock );
		shown_width = 0;
		shown_height = 0;
		ReleaseSRWLockExclusive ( &frame_lock );
		measured = 0.0;
		code = 0;

		if ( state == SESSION_RUNNING )
			state = SESSION_OFF;

	}

	void session::reset ( ) {

		if ( state != SESSION_RUNNING )
			return;

		lock_board ( );

		for ( int32_t i = 0; i < drive_count; i++ )
			save_disk ( i );

		board->power_on ( );
		unlock_board ( );
		InterlockedExchange ( &resync, 1 );

	}

	void session::set_paused ( bool value ) {

		paused = value;
		InterlockedExchange ( &resync, 1 );

	}

	void session::set_turbo ( bool value ) {

		turbo = value;
		InterlockedExchange ( &resync, 1 );

	}

	void session::set_fast_floppy ( bool value ) {

		lock_board ( );
		board->floppy.fast = value;
		unlock_board ( );

	}

	void session::save_disk ( int32_t index ) {

		machine::floppy& disk = board->floppy.drives [ index ].disk;

		if ( !disk.present ( ) || !disk.changed || !disks [ index ] [ 0 ] || machine::is_program_path ( disks [ index ] ) )
			return;

		if ( files::write ( disks [ index ], reinterpret_cast < const char* > ( disk.bytes.data ( ) ), disk.bytes.count ( ) ) )
			disk.changed = false;

	}

	bool session::insert ( int32_t index, const wchar_t* path ) {

		base::list < char > file;

		note [ 0 ] = 0;

		if ( index < 0 || index >= drive_count )
			return false;

		bool program = machine::is_program_path ( path );

		if ( !files::read ( path, file ) ) {

			if ( program ) {

				base::writer text;

				text.add ( L"Couldn't read " ).add ( file_name ( path ) );
				set_problem ( note, text.text ( ) );

			}

			return false;

		}

		base::list < uint8_t > built;
		const uint8_t* image = reinterpret_cast < const uint8_t* > ( file.data ( ) );
		uint32_t size = static_cast < uint32_t > ( file.count ( ) - 1 );
		bool protect = false;

		if ( program ) {

			char message [ 160 ] = { };
			uint32_t used = 0;
			base::writer text;

			if ( !machine::make_program_disk ( file.data ( ), static_cast < int32_t > ( size ), built, used, message, sizeof ( message ) ) ) {

				text.add ( file_name ( path ) ).add ( L", " ).add ( message );
				set_problem ( note, text.text ( ) );

				return false;

			}

			text.add ( L"Assembled " ).add ( file_name ( path ) ).add ( L" into " ).grouped ( used ).add ( used == 1 ? L" byte" : L" bytes" );
			set_problem ( note, text.text ( ) );
			image = built.data ( );
			size = static_cast < uint32_t > ( built.count ( ) );

		} else {

			if ( !machine::floppy::identify ( size ) )
				return false;

			DWORD attributes = GetFileAttributesW ( path );

			protect = attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_READONLY );

		}

		lock_board ( );
		save_disk ( index );

		bool inserted = board->floppy.insert ( index, image, size, protect );

		if ( inserted )
			copy_path ( disks [ index ], path );

		unlock_board ( );

		return inserted;

	}

	void session::eject ( int32_t index ) {

		if ( index < 0 || index >= drive_count )
			return;

		lock_board ( );
		save_disk ( index );
		board->floppy.eject ( index );
		disks [ index ] [ 0 ] = 0;
		unlock_board ( );

	}

	void session::set_protect ( int32_t index, bool value ) {

		if ( index < 0 || index >= drive_count )
			return;

		lock_board ( );

		if ( !value )
			save_disk ( index );

		board->floppy.drives [ index ].disk.protect = value;
		unlock_board ( );

	}

	void session::drive ( int32_t index, drive_info& out ) {

		out = drive_info ( );

		if ( index < 0 || index >= drive_count )
			return;

		lock_board ( );

		const machine::floppy& disk = board->floppy.drives [ index ].disk;

		copy_path ( out.name, file_name ( disks [ index ] ) );
		out.present = disk.present ( );
		out.kilobytes = disk.present ( ) ? disk.format->size / 1024 : 0;
		out.protect = disk.protect;
		out.light = ( lights >> index & 1 ) != 0;
		out.program = disk.present ( ) && machine::is_program_path ( disks [ index ] );
		unlock_board ( );

	}

	bool session::save_state ( const wchar_t* path ) {

		if ( state != SESSION_RUNNING )
			return false;

		base::list < uint8_t > machine_state;

		lock_board ( );
		board->save_state ( machine_state );
		unlock_board ( );

		base::list < char > file;
		uint32_t header = state_header;

		for ( int32_t b = 0; b < 4; b++ )
			file.push ( static_cast < char > ( header >> ( b * 8 ) ) );

		for ( int32_t d = 0; d < drive_count; d++ ) {

			for ( int32_t c = 0; c < MAX_PATH; c++ ) {

				file.push ( static_cast < char > ( disks [ d ] [ c ] ) );
				file.push ( static_cast < char > ( disks [ d ] [ c ] >> 8 ) );

			}

		}

		file.reserve ( file.size ( ) + machine_state.size ( ) );

		for ( uint8_t value : machine_state )
			file.push ( static_cast < char > ( value ) );

		return files::write ( path, file.data ( ), file.count ( ) );

	}

	bool session::load_state ( const wchar_t* path ) {

		if ( state != SESSION_RUNNING )
			return false;

		base::list < char > file;
		uint32_t fixed = 4 + drive_count * MAX_PATH * 2;

		if ( !files::read ( path, file ) || static_cast < uint32_t > ( file.count ( ) - 1 ) <= fixed )
			return false;

		const uint8_t* data = reinterpret_cast < const uint8_t* > ( file.data ( ) );
		uint32_t header = static_cast < uint32_t > ( data [ 0 ] ) | static_cast < uint32_t > ( data [ 1 ] ) << 8 | static_cast < uint32_t > ( data [ 2 ] ) << 16 | static_cast < uint32_t > ( data [ 3 ] ) << 24;

		if ( header != state_header )
			return false;

		wchar_t paths [ drive_count ] [ MAX_PATH ];

		for ( int32_t d = 0; d < drive_count; d++ ) {

			for ( int32_t c = 0; c < MAX_PATH; c++ ) {

				const uint8_t* at = data + 4 + ( d * MAX_PATH + c ) * 2;

				paths [ d ] [ c ] = static_cast < wchar_t > ( at [ 0 ] | at [ 1 ] << 8 );

			}

			paths [ d ] [ MAX_PATH - 1 ] = 0;

		}

		lock_board ( );

		for ( int32_t d = 0; d < drive_count; d++ )
			save_disk ( d );

		bool loaded = board->load_state ( data + fixed, static_cast < uint32_t > ( file.count ( ) - 1 ) - fixed );

		if ( loaded ) {

			for ( int32_t d = 0; d < drive_count; d++ )
				copy_path ( disks [ d ], board->floppy.drives [ d ].disk.present ( ) ? paths [ d ] : L"" );

		}

		unlock_board ( );
		InterlockedExchange ( &resync, 1 );

		return loaded;

	}

	void session::key ( uint8_t scan ) {

		AcquireSRWLockExclusive ( &key_lock );

		if ( key_count < sizeof ( keys ) )
			keys [ key_count++ ] = scan;

		ReleaseSRWLockExclusive ( &key_lock );

	}

	bool session::latest ( uint32_t* pixels, int32_t& width, int32_t& height ) {

		InterlockedExchange ( &posted, 0 );
		AcquireSRWLockShared ( &frame_lock );

		bool ready = shown_width > 0 && state == SESSION_RUNNING;

		if ( ready ) {

			width = shown_width;
			height = shown_height;

			for ( int32_t i = 0; i < width * height; i++ )
				pixels [ i ] = shown [ i ];

		}

		ReleaseSRWLockShared ( &frame_lock );

		return ready;

	}

	DWORD __stdcall session::loop ( void* self ) {

		static_cast < session* > ( self )->run ( );

		return 0;

	}

	void session::run ( ) {

		HANDLE timer = CreateWaitableTimerExW ( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
		LARGE_INTEGER frequency;
		LARGE_INTEGER started;
		LARGE_INTEGER sampled;
		LARGE_INTEGER presented;
		uint64_t frames = board->video.frames;
		uint64_t base_cycles = board->core.cycles;
		uint64_t sampled_cycles = base_cycles;
		float samples [ 4096 ];

		QueryPerformanceFrequency ( &frequency );
		QueryPerformanceCounter ( &started );
		sampled = started;
		presented = started;

		auto seconds_between = [ & ] ( const LARGE_INTEGER& from, const LARGE_INTEGER& to ) {

			return static_cast < double > ( to.QuadPart - from.QuadPart ) / static_cast < double > ( frequency.QuadPart );

		};

		while ( !quit ) {

			LARGE_INTEGER now;

			QueryPerformanceCounter ( &now );

			if ( InterlockedExchange ( &resync, 0 ) ) {

				started = now;
				base_cycles = board->core.cycles;
				sampled = now;
				sampled_cycles = base_cycles;

			}

			if ( paused ) {

				measured = 0.0;
				Sleep ( 10 );
				InterlockedExchange ( &resync, 1 );

				continue;

			}

			uint64_t done = board->core.cycles;
			uint64_t slice = machine::xt::clock_rate / 100;

			if ( !turbo ) {

				uint64_t target = base_cycles + static_cast < uint64_t > ( seconds_between ( started, now ) * static_cast < double > ( machine::xt::clock_rate ) );

				if ( target > done + machine::xt::clock_rate / 5 ) {

					base_cycles += target - done - machine::xt::clock_rate / 60;
					target = done + machine::xt::clock_rate / 60;

				}

				if ( target <= done ) {

					if ( timer ) {

						LARGE_INTEGER due;

						due.QuadPart = -10000;
						SetWaitableTimer ( timer, &due, 0, nullptr, nullptr, FALSE );
						WaitForSingleObject ( timer, 10 );

					} else
						Sleep ( 1 );

					continue;

				}

				slice = base::min ( target - done, slice );

			}

			AcquireSRWLockExclusive ( &board_lock );
			AcquireSRWLockExclusive ( &key_lock );

			for ( uint32_t i = 0; i < key_count; i++ )
				board->keys.press ( keys [ i ] );

			key_count = 0;
			ReleaseSRWLockExclusive ( &key_lock );
			board->run ( slice );

			int32_t count = board->take_samples ( samples, 4096 );

			if ( sound.rate && count && !turbo )
				sound.push ( samples, count );

			long spinning = 0;

			for ( int32_t i = 0; i < drive_count; i++ ) {

				if ( board->floppy.spinning ( i ) )
					spinning |= 1 << i;
				else if ( lights >> i & 1 )
					save_disk ( i );

			}

			code = board->pages.post_code;

			bool fresh = board->video.frames != frames;

			if ( fresh ) {

				frames = board->video.frames;
				AcquireSRWLockExclusive ( &frame_lock );
				shown_width = board->video.width;
				shown_height = board->video.height;

				for ( int32_t i = 0; i < shown_width * shown_height; i++ )
					shown [ i ] = board->video.pixels [ i ];

				ReleaseSRWLockExclusive ( &frame_lock );

			}

			ReleaseSRWLockExclusive ( &board_lock );

			while ( requests )
				SwitchToThread ( );

			QueryPerformanceCounter ( &now );

			if ( fresh && seconds_between ( presented, now ) > 1.0 / 75.0 && !InterlockedExchange ( &posted, 1 ) ) {

				presented = now;
				PostMessageW ( window_handle, frame_message, 0, 0 );

			}

			if ( spinning != lights ) {

				InterlockedExchange ( &lights, spinning );
				PostMessageW ( window_handle, light_message, 0, 0 );

			}

			double elapsed = seconds_between ( sampled, now );

			if ( elapsed > 0.25 ) {

				measured = static_cast < double > ( board->core.cycles - sampled_cycles ) / elapsed / 1000000.0;
				sampled = now;
				sampled_cycles = board->core.cycles;

			}

		}

		if ( timer )
			CloseHandle ( timer );

	}

}