#include "pc/storage.hh"

namespace pc {

	static bool fixed_vhd ( HANDLE handle, uint64_t length ) {

		if ( length < 512 )
			return false;

		uint8_t footer [ 512 ] = { };
		OVERLAPPED place = { };
		DWORD done = 0;
		uint64_t at = length - 512;

		place.Offset = static_cast < DWORD > ( at );
		place.OffsetHigh = static_cast < DWORD > ( at >> 32 );

		if ( !ReadFile ( handle, footer, 512, &done, &place ) || done != 512 )
			return false;

		static const char cookie [ ] = "conectix";

		for ( int32_t i = 0; i < 8; i++ ) {

			if ( footer [ i ] != static_cast < uint8_t > ( cookie [ i ] ) )
				return false;

		}

		uint32_t type = static_cast < uint32_t > ( footer [ 60 ] ) << 24 | static_cast < uint32_t > ( footer [ 61 ] ) << 16 | static_cast < uint32_t > ( footer [ 62 ] ) << 8 | footer [ 63 ];

		return type == 2;

	}

	disk_file::~disk_file ( ) {

		close ( );

	}

	bool disk_file::open ( const wchar_t* path, bool can_write ) {

		close ( );

		if ( !path || !path [ 0 ] )
			return false;

		DWORD access = can_write ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ;
		DWORD sharing = can_write ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE;

		handle = CreateFileW ( path, access, sharing, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr );

		if ( handle == INVALID_HANDLE_VALUE )
			return false;

		LARGE_INTEGER length = { };

		if ( !GetFileSizeEx ( handle, &length ) || length.QuadPart <= 0 ) {

			close ( );

			return false;

		}

		data_size = static_cast < uint64_t > ( length.QuadPart );

		if ( fixed_vhd ( handle, data_size ) )
			data_size -= 512;

		size = data_size;
		writable = can_write;

		return true;

	}

	void disk_file::close ( ) {

		if ( handle != INVALID_HANDLE_VALUE ) {

			if ( writable )
				FlushFileBuffers ( handle );

			CloseHandle ( handle );

		}

		handle = INVALID_HANDLE_VALUE;
		size = 0;
		data_size = 0;
		writable = false;

	}

	bool disk_file::read ( uint64_t offset, void* out, uint32_t count ) {

		uint8_t* target = static_cast < uint8_t* > ( out );

		if ( handle == INVALID_HANDLE_VALUE || offset > size ) {

			for ( uint32_t i = 0; i < count; i++ )
				target [ i ] = 0;

			return false;

		}

		uint32_t inside = static_cast < uint32_t > ( base::min ( static_cast < uint64_t > ( count ), size - offset ) );
		OVERLAPPED place = { };
		DWORD done = 0;

		place.Offset = static_cast < DWORD > ( offset );
		place.OffsetHigh = static_cast < DWORD > ( offset >> 32 );
		InterlockedIncrement ( &activity );

		bool ok = !inside || ( ReadFile ( handle, target, inside, &done, &place ) && done == inside );

		for ( uint32_t i = ok ? inside : 0; i < count; i++ )
			target [ i ] = 0;

		return ok && inside == count;

	}

	bool disk_file::write ( uint64_t offset, const void* data, uint32_t count ) {

		if ( handle == INVALID_HANDLE_VALUE || !writable || offset > size || count > size - offset )
			return false;

		OVERLAPPED place = { };
		DWORD done = 0;

		place.Offset = static_cast < DWORD > ( offset );
		place.OffsetHigh = static_cast < DWORD > ( offset >> 32 );
		InterlockedIncrement ( &activity );

		return WriteFile ( handle, data, count, &done, &place ) && done == count;

	}

	void disk_file::flush ( ) {

		if ( handle != INVALID_HANDLE_VALUE && writable )
			FlushFileBuffers ( handle );

	}

}