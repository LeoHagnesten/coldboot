#include "base/inflate.hh"

namespace inflate {

	struct bit_reader {

		const uint8_t* data;
		size_t size;
		size_t at = 0;
		uint32_t buffer = 0;
		int32_t count = 0;
		bool failed = false;

		uint32_t bits ( int32_t wanted ) {

			while ( count < wanted ) {

				if ( at >= size ) {

					failed = true;

					return 0;

				}

				buffer |= static_cast < uint32_t > ( data [ at++ ] ) << count;
				count += 8;

			}

			uint32_t value = buffer & ( ( 1u << wanted ) - 1 );

			buffer >>= wanted;
			count -= wanted;

			return value;

		}

		void align ( ) {

			buffer = 0;
			count = 0;

		}

	};

	struct huffman {

		int16_t counts [ 16 ] = { };
		int16_t symbols [ 320 ] = { };

		bool build ( const uint8_t* lengths, int32_t total ) {

			int16_t offsets [ 16 ] = { };

			for ( int32_t i = 0; i < 16; i++ )
				counts [ i ] = 0;

			for ( int32_t i = 0; i < total; i++ )
				counts [ lengths [ i ] ]++;

			counts [ 0 ] = 0;

			for ( int32_t i = 1; i < 16; i++ )
				offsets [ i ] = static_cast < int16_t > ( offsets [ i - 1 ] + counts [ i - 1 ] );

			for ( int32_t i = 0; i < total; i++ ) {

				if ( lengths [ i ] )
					symbols [ offsets [ lengths [ i ] ]++ ] = static_cast < int16_t > ( i );

			}

			return true;

		}

		int32_t decode ( bit_reader& in ) const {

			int32_t code = 0;
			int32_t first = 0;
			int32_t index = 0;

			for ( int32_t length = 1; length < 16; length++ ) {

				code |= static_cast < int32_t > ( in.bits ( 1 ) );

				int32_t count = counts [ length ];

				if ( code - count < first )
					return symbols [ index + ( code - first ) ];

				index += count;
				first += count;
				first <<= 1;
				code <<= 1;

				if ( in.failed )
					return -1;

			}

			return -1;

		}

	};

	static const uint16_t length_base [ 29 ] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const uint8_t length_extra [ 29 ] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	static const uint16_t distance_base [ 30 ] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
	static const uint8_t distance_extra [ 30 ] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

	static bool block ( bit_reader& in, base::list < uint8_t >& out, const huffman& literals, const huffman& distances ) {

		while ( true ) {

			int32_t symbol = literals.decode ( in );

			if ( symbol < 0 || in.failed )
				return false;

			if ( symbol < 256 ) {

				out.push ( static_cast < uint8_t > ( symbol ) );

				continue;

			}

			if ( symbol == 256 )
				return true;

			symbol -= 257;

			if ( symbol >= 29 )
				return false;

			int32_t length = length_base [ symbol ] + static_cast < int32_t > ( in.bits ( length_extra [ symbol ] ) );
			int32_t distance_symbol = distances.decode ( in );

			if ( distance_symbol < 0 || distance_symbol >= 30 )
				return false;

			size_t distance = distance_base [ distance_symbol ] + in.bits ( distance_extra [ distance_symbol ] );

			if ( distance > out.size ( ) || in.failed )
				return false;

			size_t from = out.size ( ) - distance;

			for ( int32_t i = 0; i < length; i++ )
				out.push ( out [ from + static_cast < size_t > ( i ) ] );

		}

	}

	bool raw ( const uint8_t* data, size_t size, base::list < uint8_t >& out ) {

		bit_reader in;

		in.data = data;
		in.size = size;

		bool last = false;

		while ( !last ) {

			last = in.bits ( 1 ) != 0;

			uint32_t type = in.bits ( 2 );

			if ( in.failed )
				return false;

			if ( type == 0 ) {

				in.align ( );

				if ( in.at + 4 > size )
					return false;

				uint32_t length = data [ in.at ] | static_cast < uint32_t > ( data [ in.at + 1 ] ) << 8;

				in.at += 4;

				if ( in.at + length > size )
					return false;

				for ( uint32_t i = 0; i < length; i++ )
					out.push ( data [ in.at + i ] );

				in.at += length;

				continue;

			}

			huffman literals;
			huffman distances;

			if ( type == 1 ) {

				uint8_t lengths [ 320 ];

				for ( int32_t i = 0; i < 144; i++ )
					lengths [ i ] = 8;

				for ( int32_t i = 144; i < 256; i++ )
					lengths [ i ] = 9;

				for ( int32_t i = 256; i < 280; i++ )
					lengths [ i ] = 7;

				for ( int32_t i = 280; i < 288; i++ )
					lengths [ i ] = 8;

				literals.build ( lengths, 288 );

				for ( int32_t i = 0; i < 30; i++ )
					lengths [ i ] = 5;

				distances.build ( lengths, 30 );

			} else if ( type == 2 ) {

				int32_t literal_count = static_cast < int32_t > ( in.bits ( 5 ) ) + 257;
				int32_t distance_count = static_cast < int32_t > ( in.bits ( 5 ) ) + 1;
				int32_t code_count = static_cast < int32_t > ( in.bits ( 4 ) ) + 4;
				static const uint8_t order [ 19 ] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
				uint8_t code_lengths [ 19 ] = { };

				for ( int32_t i = 0; i < code_count; i++ )
					code_lengths [ order [ i ] ] = static_cast < uint8_t > ( in.bits ( 3 ) );

				huffman codes;

				codes.build ( code_lengths, 19 );

				uint8_t lengths [ 320 ] = { };
				int32_t total = literal_count + distance_count;
				int32_t at = 0;

				while ( at < total ) {

					int32_t symbol = codes.decode ( in );

					if ( symbol < 0 || in.failed )
						return false;

					if ( symbol < 16 ) {

						lengths [ at++ ] = static_cast < uint8_t > ( symbol );

						continue;

					}

					int32_t repeat = 0;
					uint8_t value = 0;

					if ( symbol == 16 ) {

						if ( !at )
							return false;

						value = lengths [ at - 1 ];
						repeat = 3 + static_cast < int32_t > ( in.bits ( 2 ) );

					} else if ( symbol == 17 )
						repeat = 3 + static_cast < int32_t > ( in.bits ( 3 ) );
					else
						repeat = 11 + static_cast < int32_t > ( in.bits ( 7 ) );

					if ( at + repeat > total )
						return false;

					while ( repeat-- )
						lengths [ at++ ] = value;

				}

				literals.build ( lengths, literal_count );
				distances.build ( lengths + literal_count, distance_count );

			} else
				return false;

			if ( !block ( in, out, literals, distances ) )
				return false;

		}

		return true;

	}

	bool gzip ( const uint8_t* data, size_t size, base::list < uint8_t >& out ) {

		if ( size < 18 || data [ 0 ] != 0x1F || data [ 1 ] != 0x8B || data [ 2 ] != 8 )
			return false;

		uint8_t flags = data [ 3 ];
		size_t at = 10;

		if ( flags & 4 ) {

			if ( at + 2 > size )
				return false;

			at += 2 + ( data [ at ] | static_cast < size_t > ( data [ at + 1 ] ) << 8 );

		}

		if ( flags & 8 ) {

			while ( at < size && data [ at ] )
				at++;

			at++;

		}

		if ( flags & 16 ) {

			while ( at < size && data [ at ] )
				at++;

			at++;

		}

		if ( flags & 2 )
			at += 2;

		if ( at >= size )
			return false;

		uint32_t expected = data [ size - 4 ] | static_cast < uint32_t > ( data [ size - 3 ] ) << 8 | static_cast < uint32_t > ( data [ size - 2 ] ) << 16 | static_cast < uint32_t > ( data [ size - 1 ] ) << 24;

		out.clear ( );
		out.reserve ( expected + 16 );

		return raw ( data + at, size - at - 8, out ) && static_cast < uint32_t > ( out.size ( ) ) == expected;

	}

}