#pragma once

#include "base/base.hh"

namespace machine {

	bool make_blank_disk ( uint32_t size, uint32_t serial, base::list < uint8_t >& out );

}