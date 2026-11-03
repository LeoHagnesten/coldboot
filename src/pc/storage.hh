#pragma once

#include "base/base.hh"
#include <windows.h>

namespace pc {

	class disk_file {

	public:

		disk_file ( ) = default;

		disk_file ( const disk_file& ) = delete;

		disk_file& operator= ( const disk_file& ) = delete;

		~disk_file ( );

		bool open ( const wchar_t* path, bool writable );

		bool open_disposable ( const wchar_t* path );

		void close ( );

		bool disposable ( ) const {

			return overlay != INVALID_HANDLE_VALUE;

		}

		bool read ( uint64_t offset, void* out, uint32_t size );

		bool write ( uint64_t offset, const void* data, uint32_t size );

		void flush ( );

		bool present ( ) const {

			return handle != INVALID_HANDLE_VALUE;

		}

		uint64_t size = 0;
		bool writable = false;
		volatile long activity = 0;

		static constexpr uint32_t block_size = 4096;
		static constexpr uint32_t scratch_size = 0x40000;

	private:

		bool read_overlay ( uint64_t offset, uint8_t* out, uint32_t size );

		bool write_overlay ( uint64_t offset, const uint8_t* data, uint32_t size );

		bool is_written ( uint64_t block ) const {

			return ( written [ block >> 6 ] >> ( block & 63 ) & 1 ) != 0;

		}

		void cipher ( uint64_t offset, uint8_t* data, uint32_t size ) const;

		HANDLE handle = INVALID_HANDLE_VALUE;
		HANDLE overlay = INVALID_HANDLE_VALUE;
		uint64_t data_size = 0;
		uint64_t* written = nullptr;
		uint64_t blocks = 0;
		uint8_t* scratch = nullptr;
		uint32_t key [ 8 ] = { };

	};

}