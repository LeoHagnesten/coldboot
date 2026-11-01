#pragma once

#include "base/base.hh"

namespace inflate {

	bool raw ( const uint8_t* data, size_t size, base::list < uint8_t >& out );

	bool gzip ( const uint8_t* data, size_t size, base::list < uint8_t >& out );

}