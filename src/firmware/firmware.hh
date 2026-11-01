#pragma once

#include "base/base.hh"

namespace firmware {

	static constexpr uint32_t rom_origin = 0xC000;
	static constexpr uint32_t rom_size = 0x4000;

	bool build ( base::list < uint8_t >& out, char* error, int32_t error_room, int32_t* error_line );

}