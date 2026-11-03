#pragma once

#include "base/base.hh"

namespace pc {

	struct pe_info {

		uint64_t preferred = 0;
		uint32_t image_size = 0;
		uint32_t entry = 0;
		uint16_t subsystem = 0;
		uint16_t machine = 0;

	};

	bool pe_inspect ( const uint8_t* file, uint32_t size, pe_info& out );

	bool pe_place ( const uint8_t* file, uint32_t size, uint8_t* out, uint64_t base );

}