#pragma once

#include "base/base.hh"

namespace machine {

	static constexpr uint32_t program_disk_size = 368640;

	bool is_program_path ( const wchar_t* path );

	bool make_program_disk ( const char* text, int32_t length, base::list < uint8_t >& out, uint32_t& used, char* error, int32_t room );

}