#pragma once

#include "base/base.hh"

namespace pc {

	class aml {

	public:

		void byte ( uint8_t value ) {

			bytes.push ( value );

		}

		void word ( uint16_t value );

		void dword ( uint32_t value );

		void integer ( uint64_t value );

		void string ( const char* text );

		void path ( const char* text );

		int32_t open ( uint8_t code );

		int32_t open_extended ( uint8_t code );

		void close ( int32_t marker );

		void name ( const char* text );

		int32_t scope ( const char* text );

		int32_t device ( const char* text );

		int32_t method ( const char* text, uint8_t arguments, bool serialized );

		int32_t package ( uint8_t count );

		int32_t buffer ( );

		void processor ( const char* text, uint8_t id );

		void eisa ( const char* id );

		void return_value ( uint64_t value );

		base::list < uint8_t > bytes;

	};

	class resources {

	public:

		void io ( uint16_t base, uint8_t length );

		void irq ( uint16_t mask );

		void bus_range ( uint16_t first, uint16_t last );

		void io_range ( uint16_t first, uint16_t last );

		void memory_range ( uint32_t first, uint32_t last, bool cacheable );

		void memory_range_64 ( uint64_t first, uint64_t last );

		void memory_fixed ( uint32_t base, uint32_t length );

		void emit ( aml& out );

		base::list < uint8_t > bytes;

	private:

		void put16 ( uint16_t value );

		void put32 ( uint32_t value );

		void put64 ( uint64_t value );

	};

}