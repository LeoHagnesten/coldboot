#include "base/files.hh"
#include <windows.h>

namespace files {

	const wchar_t* folder ( ) {

		static wchar_t path [ MAX_PATH ] = { };

		if ( path [ 0 ] )
			return path;

		DWORD length = GetEnvironmentVariableW ( L"APPDATA", path, MAX_PATH - 16 );

		if ( !length || length >= MAX_PATH - 16 ) {

			path [ 0 ] = L'.';
			path [ 1 ] = 0;

			return path;

		}

		const wchar_t* tail = L"\\Coldboot";

		for ( int32_t i = 0; tail [ i ]; i++ )
			path [ length++ ] = tail [ i ];

		path [ length ] = 0;
		CreateDirectoryW ( path, nullptr );

		return path;

	}

	bool read ( const wchar_t* path, base::list < char >& result ) {

		result.clear ( );

		HANDLE file = CreateFileW ( path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( file == INVALID_HANDLE_VALUE )
			return false;

		LARGE_INTEGER size = { };

		GetFileSizeEx ( file, &size );

		if ( size.QuadPart > 16 * 1024 * 1024 ) {

			CloseHandle ( file );

			return false;

		}

		result.resize ( static_cast < size_t > ( size.QuadPart ) + 1 );

		DWORD done = 0;
		BOOL ok = ReadFile ( file, result.data ( ), static_cast < DWORD > ( size.QuadPart ), &done, nullptr );

		CloseHandle ( file );
		result [ static_cast < size_t > ( done ) ] = 0;
		result.resize ( static_cast < size_t > ( done ) + 1 );

		return ok != FALSE;

	}

	bool write ( const wchar_t* path, const char* data, int32_t size ) {

		wchar_t temporary [ 600 ];
		int32_t length = 0;

		for ( ; path [ length ] && length < 590; length++ )
			temporary [ length ] = path [ length ];

		const wchar_t* suffix = L".new";

		for ( int32_t i = 0; suffix [ i ]; i++ )
			temporary [ length++ ] = suffix [ i ];

		temporary [ length ] = 0;

		HANDLE file = CreateFileW ( temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( file == INVALID_HANDLE_VALUE )
			return false;

		DWORD done = 0;
		BOOL ok = WriteFile ( file, data, static_cast < DWORD > ( size ), &done, nullptr ) && FlushFileBuffers ( file );

		CloseHandle ( file );

		if ( !ok || done != static_cast < DWORD > ( size ) ) {

			DeleteFileW ( temporary );

			return false;

		}

		return MoveFileExW ( temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) != FALSE;

	}

	static SRWLOCK queue_lock = SRWLOCK_INIT;
	static SRWLOCK disk_lock = SRWLOCK_INIT;
	static base::text pending_path;
	static base::list < char > pending_data;
	static bool pending = false;
	static HANDLE writer = nullptr;
	static HANDLE writer_wake = nullptr;

	static void flush_pending ( ) {

		base::text path;
		base::list < char > data;
		bool have = false;

		AcquireSRWLockExclusive ( &disk_lock );
		AcquireSRWLockExclusive ( &queue_lock );

		if ( pending ) {

			path = pending_path;
			data = base::move ( pending_data );
			pending = false;
			have = true;

		}

		ReleaseSRWLockExclusive ( &queue_lock );

		if ( have )
			write ( path.get ( ), data.data ( ), data.count ( ) );

		ReleaseSRWLockExclusive ( &disk_lock );

	}

	static DWORD __stdcall writer_loop ( void* ) {

		while ( true ) {

			WaitForSingleObject ( writer_wake, INFINITE );
			flush_pending ( );

		}

		return 0;

	}

	void write_later ( const wchar_t* path, const char* data, int32_t size ) {

		AcquireSRWLockExclusive ( &queue_lock );

		if ( pending && !base::same_text ( pending_path.get ( ), path ) ) {

			ReleaseSRWLockExclusive ( &queue_lock );
			flush_pending ( );
			AcquireSRWLockExclusive ( &queue_lock );

		}

		pending_path.set ( path );
		pending_data.resize ( static_cast < size_t > ( size ) );

		for ( int32_t i = 0; i < size; i++ )
			pending_data [ static_cast < size_t > ( i ) ] = data [ i ];

		pending = true;

		if ( !writer ) {

			writer_wake = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
			writer = CreateThread ( nullptr, 0, &writer_loop, nullptr, 0, nullptr );

		}

		ReleaseSRWLockExclusive ( &queue_lock );

		if ( writer_wake && writer )
			SetEvent ( writer_wake );
		else
			flush_pending ( );

	}

	void settle ( ) {

		flush_pending ( );

	}

}