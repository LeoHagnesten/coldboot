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

		void close ( );

		bool read ( uint64_t offset, void* out, uint32_t size );

		bool write ( uint64_t offset, const void* data, uint32_t size );

		void flush ( );

		bool present ( ) const {

			return handle != INVALID_HANDLE_VALUE;

		}

		uint64_t size = 0;
		bool writable = false;
		volatile long activity = 0;

	private:

		HANDLE handle = INVALID_HANDLE_VALUE;
		uint64_t data_size = 0;

	};

}