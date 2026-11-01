#pragma once

#include "base/base.hh"

namespace app {

	bool remembered_bios ( wchar_t* out, int32_t room );

	void remember_bios ( const wchar_t* path );

}