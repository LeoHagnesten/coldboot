#pragma once

#include "base/base.hh"

namespace files {

	const wchar_t* folder ( );

	bool read ( const wchar_t* path, base::list < char >& result );

	bool write ( const wchar_t* path, const char* data, int32_t size );

	void write_later ( const wchar_t* path, const char* data, int32_t size );

	void settle ( );

}