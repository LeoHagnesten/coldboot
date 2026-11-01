#include "app/session.hh"
#include "base/files.hh"

namespace app {

	session::~session ( ) {

		stop ( );
		delete board;
		runtime_release ( shown );

	}

	bool session::start ( const wchar_t* bios_path, HWND window ) {

		stop ( );

		int32_t length = 0;

		for ( ; bios_path [ length ] && length < MAX_PATH - 1; length++ )
			bios [ length ] = bios_path [ length ];

		bios [ length ] = 0;
		window_handle = window;

		if ( !board )
			board = new machine::xt ( );

		base::list < char > image;

		board->set_memory ( board->memory_kilobytes );

		if ( !files::read ( bios, image ) || image.count ( ) <= 1 || !board->load_bios ( reinterpret_cast < const uint8_t* > ( image.data ( ) ), static_cast < uint32_t > ( image.count ( ) - 1 ) ) ) {

			state = SESSION_FAILED;

			return false;

		}

		if ( !shown )
			shown = static_cast < uint32_t* > ( runtime_allocate_zero ( sizeof ( uint32_t ) * machine::cga::max_width * machine::cga::max_height ) );

		if ( sound.start ( ) )
			board->sample_rate = sound.rate;

		board->power_on ( );
		key_count = 0;
		quit = 0;
		posted = 0;
		state = SESSION_RUNNING;
		thread = CreateThread ( nullptr, 0, &loop, this, 0, nullptr );

		return thread != nullptr;

	}

	void session::stop ( ) {

		if ( thread ) {

			InterlockedExchange ( &quit, 1 );
			WaitForSingleObject ( thread, INFINITE );
			CloseHandle ( thread );
			thread = nullptr;

		}

		sound.stop ( );

		if ( board ) {

			for ( int32_t i = 0; i < drive_count; i++ )
				save_disk ( i );

		}

		InterlockedExchange ( &lights, 0 );

		if ( state == SESSION_RUNNING )
			state = SESSION_EMPTY;

	}

	bool session::insert ( int32_t drive, const wchar_t* path ) {

		base::list < char > image;

		if ( drive < 0 || drive >= drive_count || !files::read ( path, image ) || !machine::floppy::identify ( static_cast < uint32_t > ( image.count ( ) - 1 ) ) )
			return false;

		DWORD attributes = GetFileAttributesW ( path );

		AcquireSRWLockExclusive ( &lock );

		disk_swap& swap = swaps [ drive ];
		int32_t length = 0;

		for ( ; path [ length ] && length < MAX_PATH - 1; length++ )
			swap.path [ length ] = path [ length ];

		swap.path [ length ] = 0;
		swap.bytes = base::move ( image );
		swap.protect = attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_READONLY );
		swap.waiting = true;

		if ( !thread ) {

			if ( !board )
				board = new machine::xt ( );

			apply_swaps ( );

		}

		ReleaseSRWLockExclusive ( &lock );

		return true;

	}

	void session::apply_swaps ( ) {

		for ( int32_t i = 0; i < drive_count; i++ ) {

			disk_swap& swap = swaps [ i ];

			if ( !swap.waiting )
				continue;

			save_disk ( i );

			if ( board->floppy.insert ( i, reinterpret_cast < const uint8_t* > ( swap.bytes.data ( ) ), static_cast < uint32_t > ( swap.bytes.count ( ) - 1 ), swap.protect ) ) {

				for ( int32_t c = 0; c < MAX_PATH; c++ )
					disks [ i ] [ c ] = swap.path [ c ];

			}

			swap.bytes.clear ( );
			swap.waiting = false;

		}

	}

	void session::save_disk ( int32_t drive ) {

		machine::floppy& disk = board->floppy.drives [ drive ].disk;

		if ( !disk.present ( ) || !disk.changed || !disks [ drive ] [ 0 ] )
			return;

		if ( files::write ( disks [ drive ], reinterpret_cast < const char* > ( disk.bytes.data ( ) ), disk.bytes.count ( ) ) )
			disk.changed = false;

	}

	bool session::disk_name ( int32_t drive, wchar_t* out, int32_t room ) {

		AcquireSRWLockShared ( &lock );

		const wchar_t* path = disks [ drive ];
		const wchar_t* name = path;

		for ( const wchar_t* at = path; *at; at++ ) {

			if ( *at == L'\\' || *at == L'/' )
				name = at + 1;

		}

		int32_t length = 0;

		for ( ; name [ length ] && length < room - 1; length++ )
			out [ length ] = name [ length ];

		out [ length ] = 0;
		ReleaseSRWLockShared ( &lock );

		return length > 0;

	}

	void session::key ( uint8_t scan ) {

		AcquireSRWLockExclusive ( &lock );

		if ( key_count < sizeof ( keys ) )
			keys [ key_count++ ] = scan;

		ReleaseSRWLockExclusive ( &lock );

	}

	bool session::latest ( uint32_t* pixels, int32_t& width, int32_t& height ) {

		InterlockedExchange ( &posted, 0 );

		if ( state != SESSION_RUNNING || !shown_width )
			return false;

		AcquireSRWLockShared ( &lock );
		width = shown_width;
		height = shown_height;

		for ( int32_t i = 0; i < width * height; i++ )
			pixels [ i ] = shown [ i ];

		ReleaseSRWLockShared ( &lock );

		return true;

	}

	DWORD __stdcall session::loop ( void* self ) {

		static_cast < session* > ( self )->run ( );

		return 0;

	}

	void session::run ( ) {

		HANDLE timer = CreateWaitableTimerExW ( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
		LARGE_INTEGER frequency;
		LARGE_INTEGER started;
		uint64_t frames = board->video.frames;
		uint64_t base_cycles = board->core.cycles;
		float samples [ 4096 ];

		QueryPerformanceFrequency ( &frequency );
		QueryPerformanceCounter ( &started );

		while ( !quit ) {

			LARGE_INTEGER now;

			QueryPerformanceCounter ( &now );

			double seconds = static_cast < double > ( now.QuadPart - started.QuadPart ) / static_cast < double > ( frequency.QuadPart );
			uint64_t target = base_cycles + static_cast < uint64_t > ( seconds * static_cast < double > ( machine::xt::clock_rate ) );
			uint64_t done = board->core.cycles;

			if ( target > done + machine::xt::clock_rate / 5 ) {

				base_cycles += target - done - machine::xt::clock_rate / 60;
				target = done + machine::xt::clock_rate / 60;

			}

			if ( target > done ) {

				AcquireSRWLockExclusive ( &lock );

				for ( uint32_t i = 0; i < key_count; i++ )
					board->keys.press ( keys [ i ] );

				key_count = 0;
				apply_swaps ( );
				ReleaseSRWLockExclusive ( &lock );

				board->run ( base::min ( target - done, static_cast < uint64_t > ( machine::xt::clock_rate / 100 ) ) );

				long spinning = 0;

				for ( int32_t i = 0; i < drive_count; i++ ) {

					if ( board->floppy.spinning ( i ) )
						spinning |= 1 << i;
					else if ( lights >> i & 1 )
						save_disk ( i );

				}

				if ( spinning != lights ) {

					InterlockedExchange ( &lights, spinning );
					PostMessageW ( window_handle, light_message, 0, 0 );

				}

				int32_t count = board->take_samples ( samples, 4096 );

				if ( sound.rate && count )
					sound.push ( samples, count );

				if ( board->video.frames != frames ) {

					frames = board->video.frames;
					AcquireSRWLockExclusive ( &lock );
					shown_width = board->video.width;
					shown_height = board->video.height;

					for ( int32_t i = 0; i < shown_width * shown_height; i++ )
						shown [ i ] = board->video.pixels [ i ];

					ReleaseSRWLockExclusive ( &lock );

					if ( !InterlockedExchange ( &posted, 1 ) )
						PostMessageW ( window_handle, frame_message, 0, 0 );

				}

				continue;

			}

			if ( timer ) {

				LARGE_INTEGER due;

				due.QuadPart = -10000;
				SetWaitableTimer ( timer, &due, 0, nullptr, nullptr, FALSE );
				WaitForSingleObject ( timer, 10 );

			} else
				Sleep ( 1 );

		}

		if ( timer )
			CloseHandle ( timer );

	}

}