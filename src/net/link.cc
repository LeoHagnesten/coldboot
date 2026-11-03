#include "net/link.hh"

namespace net {

	uint32_t sum_words ( const uint8_t* data, uint32_t size, uint32_t sum ) {

		uint32_t i = 0;

		for ( ; i + 1 < size; i += 2 )
			sum += static_cast < uint32_t > ( data [ i ] ) << 8 | data [ i + 1 ];

		if ( i < size )
			sum += static_cast < uint32_t > ( data [ i ] ) << 8;

		return sum;

	}

	uint16_t fold ( uint32_t sum ) {

		while ( sum >> 16 )
			sum = ( sum & 0xFFFF ) + ( sum >> 16 );

		return static_cast < uint16_t > ( ~sum & 0xFFFF );

	}

	uint32_t crc32 ( const uint8_t* data, uint32_t size ) {

		uint32_t crc = 0xFFFFFFFFu;

		for ( uint32_t i = 0; i < size; i++ ) {

			crc ^= data [ i ];

			for ( int32_t bit = 0; bit < 8; bit++ )
				crc = crc & 1 ? ( crc >> 1 ) ^ 0xEDB88320u : crc >> 1;

		}

		return ~crc;

	}

}