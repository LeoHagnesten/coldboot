#pragma once

#include "base/base.hh"

namespace assembler {

	struct blob {

		const char* name;
		const uint8_t* data;
		uint32_t size;

	};

	struct symbol {

		char name [ 48 ];
		uint32_t value;

	};

	struct source_line {

		uint32_t address;
		int32_t line;

	};

	struct result {

		base::list < uint8_t > bytes;
		base::list < symbol > symbols;
		base::list < source_line > lines;
		uint32_t origin = 0;
		int32_t error_line = 0;
		char error [ 160 ] = { };

	};

	bool assemble ( const char* text, int32_t length, const blob* blobs, int32_t blob_count, result& out );

	bool find ( const result& image, const char* name, uint32_t& value );

}