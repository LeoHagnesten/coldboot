#pragma once

#include "base/base.hh"

namespace png {

	bool save ( const wchar_t* path, const uint32_t* pixels, int32_t width, int32_t height );

}