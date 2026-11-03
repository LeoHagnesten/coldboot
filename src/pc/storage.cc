#include "pc/storage.hh"
#include <winioctl.h>
#include <ntsecapi.h>

namespace pc {

	static uint32_t rotate ( uint32_t value, int32_t count ) {

		return value << count | value >> ( 32 - count );

	}

	static void quarter ( uint32_t* state, int32_t a, int32_t b, int32_t c, int32_t d ) {

		state [ a ] += state [ b ];
		state [ d ] = rotate ( state [ d ] ^ state [ a ], 16 );
		state [ c ] += state [ d ];
		state [ b ] = rotate ( state [ b ] ^ state [ c ], 12 );
		state [ a ] += state [ b ];
		state [ d ] = rotate ( state [ d ] ^ state [ a ], 8 );
		state [ c ] += state [ d ];
		state [ b ] = rotate ( state [ b ] ^ state [ c ], 7 );

	}

	static void chacha_block ( const uint32_t* key, uint64_t counter, uint8_t* out ) {

		uint32_t input [ 16 ] = {

			0x61707865, 0x3320646E, 0x79622D32, 0x6B206574,
			key [ 0 ], key [ 1 ], key [ 2 ], key [ 3 ], key [ 4 ], key [ 5 ], key [ 6 ], key [ 7 ],
			static_cast < uint32_t > ( counter ), static_cast < uint32_t > ( counter >> 32 ), 0x434F4C44, 0x424F4F54,

		};
		uint32_t state [ 16 ];

		for ( int32_t i = 0; i < 16; i++ )
			state [ i ] = input [ i ];

		for ( int32_t round = 0; round < 10; round++ ) {

			quarter ( state, 0, 4, 8, 12 );
			quarter ( state, 1, 5, 9, 13 );
			quarter ( state, 2, 6, 10, 14 );
			quarter ( state, 3, 7, 11, 15 );
			quarter ( state, 0, 5, 10, 15 );
			quarter ( state, 1, 6, 11, 12 );
			quarter ( state, 2, 7, 8, 13 );
			quarter ( state, 3, 4, 9, 14 );

		}

		for ( int32_t i = 0; i < 16; i++ ) {

			uint32_t word = state [ i ] + input [ i ];

			out [ i * 4 ] = static_cast < uint8_t > ( word );
			out [ i * 4 + 1 ] = static_cast < uint8_t > ( word >> 8 );
			out [ i * 4 + 2 ] = static_cast < uint8_t > ( word >> 16 );
			out [ i * 4 + 3 ] = static_cast < uint8_t > ( word >> 24 );

		}

	}

	static bool read_raw ( HANDLE file, uint64_t offset, void* out, uint32_t size ) {

		OVERLAPPED place = { };
		DWORD done = 0;

		place.Offset = static_cast < DWORD > ( offset );
		place.OffsetHigh = static_cast < DWORD > ( offset >> 32 );

		return !size || ( ReadFile ( file, out, size, &done, &place ) && done == size );

	}

	static bool write_raw ( HANDLE file, uint64_t offset, const void* data, uint32_t size ) {

		OVERLAPPED place = { };
		DWORD done = 0;

		place.Offset = static_cast < DWORD > ( offset );
		place.OffsetHigh = static_cast < DWORD > ( offset >> 32 );

		return !size || ( WriteFile ( file, data, size, &done, &place ) && done == size );

	}

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

	bool disk_file::open_disposable ( const wchar_t* path ) {

		if ( !open ( path, false ) )
			return false;

		wchar_t folder [ MAX_PATH ] = { };
		wchar_t name [ MAX_PATH ] = { };

		blocks = ( data_size + block_size - 1 ) / block_size;
		written = static_cast < uint64_t* > ( VirtualAlloc ( nullptr, static_cast < size_t > ( ( blocks + 63 ) / 64 * 8 ), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );
		scratch = static_cast < uint8_t* > ( VirtualAlloc ( nullptr, scratch_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );

		if ( !written || !scratch || !RtlGenRandom ( key, sizeof ( key ) ) || !GetTempPathW ( MAX_PATH, folder ) || !GetTempFileNameW ( folder, L"cbd", 0, name ) ) {

			close ( );

			return false;

		}

		overlay = CreateFileW ( name, GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_RANDOM_ACCESS, nullptr );

		if ( overlay == INVALID_HANDLE_VALUE ) {

			DeleteFileW ( name );
			close ( );

			return false;

		}

		DWORD returned = 0;
		LARGE_INTEGER end;

		end.QuadPart = static_cast < LONGLONG > ( data_size );
		DeviceIoControl ( overlay, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr );

		if ( !SetFilePointerEx ( overlay, end, nullptr, FILE_BEGIN ) || !SetEndOfFile ( overlay ) ) {

			close ( );

			return false;

		}

		writable = true;

		return true;

	}

	void disk_file::close ( ) {

		if ( handle != INVALID_HANDLE_VALUE ) {

			if ( writable && overlay == INVALID_HANDLE_VALUE )
				FlushFileBuffers ( handle );

			CloseHandle ( handle );

		}

		if ( overlay != INVALID_HANDLE_VALUE )
			CloseHandle ( overlay );

		if ( written )
			VirtualFree ( written, 0, MEM_RELEASE );

		if ( scratch )
			VirtualFree ( scratch, 0, MEM_RELEASE );

		SecureZeroMemory ( key, sizeof ( key ) );
		handle = INVALID_HANDLE_VALUE;
		overlay = INVALID_HANDLE_VALUE;
		written = nullptr;
		scratch = nullptr;
		blocks = 0;
		size = 0;
		data_size = 0;
		writable = false;

	}

	void disk_file::cipher ( uint64_t offset, uint8_t* data, uint32_t count ) const {

		uint8_t stream [ 64 ];
		uint32_t done = 0;

		while ( done < count ) {

			uint64_t at = offset + done;
			uint32_t within = static_cast < uint32_t > ( at & 63 );
			uint32_t take = base::min ( 64u - within, count - done );

			chacha_block ( key, at >> 6, stream );

			for ( uint32_t i = 0; i < take; i++ )
				data [ done + i ] ^= stream [ within + i ];

			done += take;

		}

		SecureZeroMemory ( stream, sizeof ( stream ) );

	}

	bool disk_file::read_overlay ( uint64_t offset, uint8_t* out, uint32_t count ) {

		uint32_t done = 0;

		while ( done < count ) {

			uint64_t at = offset + done;
			uint64_t block = at / block_size;
			bool state = is_written ( block );
			uint64_t run_end = ( block + 1 ) * block_size;

			while ( run_end < at + ( count - done ) && run_end / block_size < blocks && is_written ( run_end / block_size ) == state )
				run_end += block_size;

			uint32_t take = static_cast < uint32_t > ( base::min ( run_end - at, static_cast < uint64_t > ( count - done ) ) );

			if ( !read_raw ( state ? overlay : handle, at, out + done, take ) )
				return false;

			if ( state )
				cipher ( at, out + done, take );

			done += take;

		}

		return true;

	}

	bool disk_file::write_overlay ( uint64_t offset, const uint8_t* data, uint32_t count ) {

		uint32_t done = 0;

		while ( done < count ) {

			uint64_t at = offset + done;
			uint64_t block = at / block_size;
			uint32_t within = static_cast < uint32_t > ( at % block_size );
			uint32_t left = count - done;

			if ( within || left < block_size ) {

				uint32_t take = base::min ( block_size - within, left );

				if ( is_written ( block ) ) {

					for ( uint32_t i = 0; i < take; i++ )
						scratch [ i ] = data [ done + i ];

					cipher ( at, scratch, take );

					if ( !write_raw ( overlay, at, scratch, take ) )
						return false;

				} else {

					uint64_t start = block * block_size;
					uint32_t length = static_cast < uint32_t > ( base::min ( static_cast < uint64_t > ( block_size ), data_size - start ) );

					if ( !read_raw ( handle, start, scratch, length ) )
						return false;

					for ( uint32_t i = 0; i < take; i++ )
						scratch [ within + i ] = data [ done + i ];

					cipher ( start, scratch, length );

					if ( !write_raw ( overlay, start, scratch, length ) )
						return false;

					written [ block >> 6 ] |= 1ull << ( block & 63 );

				}

				done += take;

				continue;

			}

			uint32_t take = base::min ( left / block_size * block_size, scratch_size );

			for ( uint32_t i = 0; i < take; i++ )
				scratch [ i ] = data [ done + i ];

			cipher ( at, scratch, take );

			if ( !write_raw ( overlay, at, scratch, take ) )
				return false;

			for ( uint64_t i = block; i < block + take / block_size; i++ )
				written [ i >> 6 ] |= 1ull << ( i & 63 );

			done += take;

		}

		return true;

	}

	bool disk_file::read ( uint64_t offset, void* out, uint32_t count ) {

		uint8_t* target = static_cast < uint8_t* > ( out );

		if ( handle == INVALID_HANDLE_VALUE || offset > size ) {

			for ( uint32_t i = 0; i < count; i++ )
				target [ i ] = 0;

			return false;

		}

		uint32_t inside = static_cast < uint32_t > ( base::min ( static_cast < uint64_t > ( count ), size - offset ) );

		InterlockedIncrement ( &activity );

		bool ok = overlay != INVALID_HANDLE_VALUE ? read_overlay ( offset, target, inside ) : read_raw ( handle, offset, target, inside );

		for ( uint32_t i = ok ? inside : 0; i < count; i++ )
			target [ i ] = 0;

		return ok && inside == count;

	}

	bool disk_file::write ( uint64_t offset, const void* data, uint32_t count ) {

		if ( handle == INVALID_HANDLE_VALUE || !writable || offset > size || count > size - offset )
			return false;

		InterlockedIncrement ( &activity );

		if ( overlay != INVALID_HANDLE_VALUE )
			return write_overlay ( offset, static_cast < const uint8_t* > ( data ), count );

		return write_raw ( handle, offset, data, count );

	}

	void disk_file::flush ( ) {

		if ( handle != INVALID_HANDLE_VALUE && writable && overlay == INVALID_HANDLE_VALUE )
			FlushFileBuffers ( handle );

	}

}