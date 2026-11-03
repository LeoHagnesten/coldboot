#pragma once

#include "base/base.hh"

namespace pc {

	struct smbios_layout {

		uint32_t processors = 1;
		uint64_t low_memory = 0;
		uint64_t high_memory = 0;
		uint32_t table_address = 0xF0020;
		uint64_t seed = 0;

	};

	struct smbios_image {

		uint8_t entry [ 31 ] = { };
		base::list < uint8_t > table;
		uint16_t structures = 0;

	};

	void build_smbios ( const smbios_layout& layout, smbios_image& out );

}