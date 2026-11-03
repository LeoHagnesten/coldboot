#include "pc/aml.hh"

namespace pc {

	void aml::word ( uint16_t value ) {

		byte ( static_cast < uint8_t > ( value ) );
		byte ( static_cast < uint8_t > ( value >> 8 ) );

	}

	void aml::dword ( uint32_t value ) {

		word ( static_cast < uint16_t > ( value ) );
		word ( static_cast < uint16_t > ( value >> 16 ) );

	}

	void aml::integer ( uint64_t value ) {

		if ( value == 0 )
			byte ( 0x00 );
		else if ( value == 1 )
			byte ( 0x01 );
		else if ( value <= 0xFF ) {

			byte ( 0x0A );
			byte ( static_cast < uint8_t > ( value ) );

		} else if ( value <= 0xFFFF ) {

			byte ( 0x0B );
			word ( static_cast < uint16_t > ( value ) );

		} else if ( value <= 0xFFFFFFFFull ) {

			byte ( 0x0C );
			dword ( static_cast < uint32_t > ( value ) );

		} else {

			byte ( 0x0E );
			dword ( static_cast < uint32_t > ( value ) );
			dword ( static_cast < uint32_t > ( value >> 32 ) );

		}

	}

	void aml::string ( const char* text ) {

		byte ( 0x0D );

		for ( int32_t i = 0; text [ i ]; i++ )
			byte ( static_cast < uint8_t > ( text [ i ] ) );

		byte ( 0x00 );

	}

	void aml::path ( const char* text ) {

		int32_t at = 0;

		while ( text [ at ] == '\\' || text [ at ] == '^' )
			byte ( static_cast < uint8_t > ( text [ at++ ] ) );

		int32_t segments = 0;

		for ( int32_t i = at; text [ i ]; i++ ) {

			if ( i == at || text [ i - 1 ] == '.' )
				segments++;

		}

		if ( segments == 0 ) {

			byte ( 0x00 );

			return;

		}

		if ( segments == 2 )
			byte ( 0x2E );
		else if ( segments > 2 ) {

			byte ( 0x2F );
			byte ( static_cast < uint8_t > ( segments ) );

		}

		while ( text [ at ] ) {

			int32_t length = 0;

			while ( text [ at + length ] && text [ at + length ] != '.' )
				length++;

			for ( int32_t i = 0; i < 4; i++ )
				byte ( static_cast < uint8_t > ( i < length ? text [ at + i ] : '_' ) );

			at += length;

			if ( text [ at ] == '.' )
				at++;

		}

	}

	int32_t aml::open ( uint8_t code ) {

		byte ( code );

		int32_t marker = bytes.count ( );

		for ( int32_t i = 0; i < 4; i++ )
			byte ( 0 );

		return marker;

	}

	int32_t aml::open_extended ( uint8_t code ) {

		byte ( 0x5B );

		return open ( code );

	}

	void aml::close ( int32_t marker ) {

		uint32_t content = static_cast < uint32_t > ( bytes.count ( ) - marker - 4 );
		uint32_t width = content + 1 <= 0x3F ? 1 : ( content + 2 <= 0xFFF ? 2 : ( content + 3 <= 0xFFFFF ? 3 : 4 ) );
		uint32_t total = content + width;
		uint8_t* data = bytes.data ( );

		if ( width == 1 )
			data [ marker ] = static_cast < uint8_t > ( total );
		else {

			data [ marker ] = static_cast < uint8_t > ( ( width - 1 ) << 6 | ( total & 0x0F ) );

			for ( uint32_t i = 1; i < width; i++ )
				data [ marker + i ] = static_cast < uint8_t > ( total >> ( 4 + ( i - 1 ) * 8 ) );

		}

		uint32_t gap = 4 - width;

		if ( !gap )
			return;

		for ( uint32_t i = 0; i < content; i++ )
			data [ marker + width + i ] = data [ marker + 4 + i ];

		bytes.resize ( bytes.size ( ) - gap );

	}

	void aml::name ( const char* text ) {

		byte ( 0x08 );
		path ( text );

	}

	int32_t aml::scope ( const char* text ) {

		int32_t marker = open ( 0x10 );

		path ( text );

		return marker;

	}

	int32_t aml::device ( const char* text ) {

		int32_t marker = open_extended ( 0x82 );

		path ( text );

		return marker;

	}

	int32_t aml::method ( const char* text, uint8_t arguments, bool serialized ) {

		int32_t marker = open ( 0x14 );

		path ( text );
		byte ( static_cast < uint8_t > ( ( arguments & 7 ) | ( serialized ? 0x08 : 0 ) ) );

		return marker;

	}

	int32_t aml::package ( uint8_t count ) {

		int32_t marker = open ( 0x12 );

		byte ( count );

		return marker;

	}

	int32_t aml::buffer ( ) {

		return open ( 0x11 );

	}

	void aml::processor ( const char* text, uint8_t id ) {

		int32_t marker = open_extended ( 0x83 );

		path ( text );
		byte ( id );
		dword ( 0 );
		byte ( 0 );
		close ( marker );

	}

	void aml::eisa ( const char* id ) {

		uint32_t letters = static_cast < uint32_t > ( ( id [ 0 ] - '@' ) & 31 ) << 10 | static_cast < uint32_t > ( ( id [ 1 ] - '@' ) & 31 ) << 5 | static_cast < uint32_t > ( ( id [ 2 ] - '@' ) & 31 );
		uint32_t digits = 0;

		for ( int32_t i = 3; i < 7; i++ ) {

			char c = id [ i ];
			uint32_t nibble = c >= '0' && c <= '9' ? static_cast < uint32_t > ( c - '0' ) : static_cast < uint32_t > ( ( c | 0x20 ) - 'a' + 10 );

			digits = digits << 4 | ( nibble & 15 );

		}

		uint32_t value = ( letters >> 8 & 0xFF ) | ( letters & 0xFF ) << 8 | ( digits >> 8 & 0xFF ) << 16 | ( digits & 0xFF ) << 24;

		byte ( 0x0C );
		dword ( value );

	}

	void aml::return_value ( uint64_t value ) {

		byte ( 0xA4 );
		integer ( value );

	}

	void resources::put16 ( uint16_t value ) {

		bytes.push ( static_cast < uint8_t > ( value ) );
		bytes.push ( static_cast < uint8_t > ( value >> 8 ) );

	}

	void resources::put32 ( uint32_t value ) {

		put16 ( static_cast < uint16_t > ( value ) );
		put16 ( static_cast < uint16_t > ( value >> 16 ) );

	}

	void resources::put64 ( uint64_t value ) {

		put32 ( static_cast < uint32_t > ( value ) );
		put32 ( static_cast < uint32_t > ( value >> 32 ) );

	}

	void resources::io ( uint16_t base, uint8_t length ) {

		bytes.push ( 0x47 );
		bytes.push ( 0x01 );
		put16 ( base );
		put16 ( base );
		bytes.push ( 0x01 );
		bytes.push ( length );

	}

	void resources::irq ( uint16_t mask ) {

		bytes.push ( 0x22 );
		put16 ( mask );

	}

	void resources::bus_range ( uint16_t first, uint16_t last ) {

		bytes.push ( 0x88 );
		put16 ( 13 );
		bytes.push ( 0x02 );
		bytes.push ( 0x0C );
		bytes.push ( 0x00 );
		put16 ( 0 );
		put16 ( first );
		put16 ( last );
		put16 ( 0 );
		put16 ( static_cast < uint16_t > ( last - first + 1 ) );

	}

	void resources::io_range ( uint16_t first, uint16_t last ) {

		bytes.push ( 0x88 );
		put16 ( 13 );
		bytes.push ( 0x01 );
		bytes.push ( 0x0C );
		bytes.push ( 0x03 );
		put16 ( 0 );
		put16 ( first );
		put16 ( last );
		put16 ( 0 );
		put16 ( static_cast < uint16_t > ( last - first + 1 ) );

	}

	void resources::memory_range ( uint32_t first, uint32_t last, bool cacheable ) {

		bytes.push ( 0x87 );
		put16 ( 23 );
		bytes.push ( 0x00 );
		bytes.push ( 0x0C );
		bytes.push ( static_cast < uint8_t > ( cacheable ? 0x03 : 0x01 ) );
		put32 ( 0 );
		put32 ( first );
		put32 ( last );
		put32 ( 0 );
		put32 ( last - first + 1 );

	}

	void resources::memory_range_64 ( uint64_t first, uint64_t last ) {

		bytes.push ( 0x8A );
		put16 ( 43 );
		bytes.push ( 0x00 );
		bytes.push ( 0x0C );
		bytes.push ( 0x03 );
		put64 ( 0 );
		put64 ( first );
		put64 ( last );
		put64 ( 0 );
		put64 ( last - first + 1 );

	}

	void resources::memory_fixed ( uint32_t base, uint32_t length ) {

		bytes.push ( 0x86 );
		put16 ( 9 );
		bytes.push ( 0x01 );
		put32 ( base );
		put32 ( length );

	}

	void resources::emit ( aml& out ) {

		bytes.push ( 0x79 );
		bytes.push ( 0x00 );

		int32_t marker = out.buffer ( );

		out.integer ( bytes.size ( ) );

		for ( uint8_t value : bytes )
			out.byte ( value );

		out.close ( marker );

	}

}