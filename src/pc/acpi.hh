#pragma once

#include "base/base.hh"

namespace pc {

	struct acpi_layout {

		uint32_t processors = 1;
		uint64_t low_memory = 0;
		uint64_t high_memory = 0;
		uint32_t tables = 0;

	};

	struct acpi_image {

		base::list < uint8_t > tables;
		uint8_t rsdp [ 36 ] = { };

	};

	void build_acpi ( const acpi_layout& layout, acpi_image& out );

	void dsdt_body ( const acpi_layout& layout, base::list < uint8_t >& out );

	uint8_t acpi_sum ( const uint8_t* data, uint32_t length );

}