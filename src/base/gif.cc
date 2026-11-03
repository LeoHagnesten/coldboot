#include "base/gif.hh"
#include <windows.h>

namespace gif {

	static constexpr uint32_t bins = 32768;
	static constexpr uint32_t table_size = 8191;
	static constexpr uint8_t transparent = 255;

	static uint32_t bin_of ( uint32_t color ) {

		return ( color >> 19 & 31 ) << 10 | ( color >> 11 & 31 ) << 5 | ( color >> 3 & 31 );

	}

	struct packer {

		base::list < uint8_t >& out;
		uint8_t block [ 255 ] = { };
		uint32_t used = 0;
		uint32_t bits = 0;
		uint32_t count = 0;

		explicit packer ( base::list < uint8_t >& target ) : out ( target ) { }

		void byte ( uint8_t value ) {

			block [ used++ ] = value;

			if ( used == 255 ) {

				out.push ( 255 );

				for ( uint32_t i = 0; i < 255; i++ )
					out.push ( block [ i ] );

				used = 0;

			}

		}

		void code ( uint32_t value, uint32_t size ) {

			bits |= value << count;
			count += size;

			while ( count >= 8 ) {

				byte ( static_cast < uint8_t > ( bits ) );
				bits >>= 8;
				count -= 8;

			}

		}

		void finish ( ) {

			if ( count )
				byte ( static_cast < uint8_t > ( bits ) );

			if ( used ) {

				out.push ( static_cast < uint8_t > ( used ) );

				for ( uint32_t i = 0; i < used; i++ )
					out.push ( block [ i ] );

			}

			out.push ( 0 );

		}

	};

	static void compress ( const uint8_t* indices, uint32_t total, base::list < uint8_t >& out ) {

		static uint32_t keys [ table_size ];
		static uint16_t codes [ table_size ];
		packer bits ( out );
		uint32_t running = 258;
		uint32_t size = 9;
		uint32_t limit = 512;

		auto clear = [ & ] ( ) {

			for ( uint32_t i = 0; i < table_size; i++ )
				keys [ i ] = 0xFFFFFFFFu;

		};

		auto emit = [ & ] ( uint32_t value ) {

			bits.code ( value, size );

			if ( running >= limit && value <= 4095 && size < 12 ) {

				size++;
				limit = 1u << size;

			}

		};

		out.push ( 8 );
		clear ( );
		emit ( 256 );

		uint32_t prefix = total ? indices [ 0 ] : 0;

		for ( uint32_t i = 1; i < total; i++ ) {

			uint32_t pixel = indices [ i ];
			uint32_t key = prefix << 8 | pixel;
			uint32_t slot = key % table_size;
			int32_t found = -1;

			while ( keys [ slot ] != 0xFFFFFFFFu ) {

				if ( keys [ slot ] == key ) {

					found = codes [ slot ];

					break;

				}

				slot = ( slot + 1 ) % table_size;

			}

			if ( found >= 0 ) {

				prefix = static_cast < uint32_t > ( found );

				continue;

			}

			emit ( prefix );
			prefix = pixel;

			if ( running >= 4095 ) {

				emit ( 256 );
				running = 258;
				size = 9;
				limit = 512;
				clear ( );

			} else {

				keys [ slot ] = key;
				codes [ slot ] = static_cast < uint16_t > ( running++ );

			}

		}

		if ( total )
			emit ( prefix );

		emit ( 257 );
		bits.finish ( );

	}

	writer::~writer ( ) {

		close ( );

	}

	void writer::put ( const void* data, uint32_t size ) {

		DWORD done = 0;

		if ( file && !WriteFile ( static_cast < HANDLE > ( file ), data, size, &done, nullptr ) )
			failed = true;

	}

	bool writer::open ( const wchar_t* path, int32_t across, int32_t down ) {

		close ( );

		HANDLE handle = CreateFileW ( path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( handle == INVALID_HANDLE_VALUE )
			return false;

		file = handle;
		width = across;
		height = down;
		frames = 0;
		first = true;
		failed = false;
		held.clear ( );
		current = static_cast < uint32_t* > ( runtime_allocate_zero ( sizeof ( uint32_t ) * static_cast < size_t > ( width ) * height ) );
		shown = static_cast < uint32_t* > ( runtime_allocate_zero ( sizeof ( uint32_t ) * static_cast < size_t > ( width ) * height ) );

		uint8_t header [ 13 ] = { 'G', 'I', 'F', '8', '9', 'a', static_cast < uint8_t > ( width ), static_cast < uint8_t > ( width >> 8 ), static_cast < uint8_t > ( height ), static_cast < uint8_t > ( height >> 8 ), 0, 0, 0 };
		static const uint8_t loop [ 19 ] = { 0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00 };

		put ( header, sizeof ( header ) );
		put ( loop, sizeof ( loop ) );

		return !failed;

	}

	void writer::scale ( const uint32_t* pixels, int32_t source_width, int32_t source_height, bool stretch ) {

		int32_t target_width = width;
		int32_t target_height = height;

		if ( !stretch ) {

			if ( static_cast < int64_t > ( source_width ) * height > static_cast < int64_t > ( source_height ) * width )
				target_height = static_cast < int32_t > ( static_cast < int64_t > ( width ) * source_height / source_width );
			else
				target_width = static_cast < int32_t > ( static_cast < int64_t > ( height ) * source_width / source_height );

		}

		int32_t left = ( width - target_width ) / 2;
		int32_t top = ( height - target_height ) / 2;

		for ( int32_t y = 0; y < height; y++ ) {

			uint32_t* row = current + static_cast < size_t > ( y ) * width;
			int32_t inside_y = y - top;

			if ( inside_y < 0 || inside_y >= target_height ) {

				for ( int32_t x = 0; x < width; x++ )
					row [ x ] = 0;

				continue;

			}

			int32_t from_y = static_cast < int32_t > ( static_cast < int64_t > ( inside_y ) * source_height / target_height );
			int32_t to_y = base::max ( from_y + 1, static_cast < int32_t > ( static_cast < int64_t > ( inside_y + 1 ) * source_height / target_height ) );

			for ( int32_t x = 0; x < width; x++ ) {

				int32_t inside_x = x - left;

				if ( inside_x < 0 || inside_x >= target_width ) {

					row [ x ] = 0;

					continue;

				}

				int32_t from_x = static_cast < int32_t > ( static_cast < int64_t > ( inside_x ) * source_width / target_width );
				int32_t to_x = base::max ( from_x + 1, static_cast < int32_t > ( static_cast < int64_t > ( inside_x + 1 ) * source_width / target_width ) );
				uint32_t red = 0;
				uint32_t green = 0;
				uint32_t blue = 0;
				uint32_t count = 0;

				for ( int32_t sy = from_y; sy < to_y; sy++ ) {

					const uint32_t* source = pixels + static_cast < size_t > ( sy ) * source_width;

					for ( int32_t sx = from_x; sx < to_x; sx++ ) {

						red += source [ sx ] >> 16 & 255;
						green += source [ sx ] >> 8 & 255;
						blue += source [ sx ] & 255;
						count++;

					}

				}

				row [ x ] = ( red / count ) << 16 | ( green / count ) << 8 | ( blue / count );

			}

		}

	}

	void writer::frame ( const uint32_t* pixels, int32_t source_width, int32_t source_height, uint32_t delay, bool stretch ) {

		if ( !file || !pixels || source_width <= 0 || source_height <= 0 )
			return;

		scale ( pixels, source_width, source_height, stretch );

		int32_t left = width;
		int32_t top = height;
		int32_t right = -1;
		int32_t bottom = -1;

		for ( int32_t y = 0; y < height; y++ ) {

			const uint32_t* now = current + static_cast < size_t > ( y ) * width;
			const uint32_t* before = shown + static_cast < size_t > ( y ) * width;

			for ( int32_t x = 0; x < width; x++ ) {

				if ( first || now [ x ] != before [ x ] ) {

					left = base::min ( left, x );
					right = base::max ( right, x );
					top = base::min ( top, y );
					bottom = base::max ( bottom, y );

				}

			}

		}

		if ( right < 0 ) {

			held_delay += delay;

			return;

		}

		flush ( );
		encode ( left, top, right + 1, bottom + 1 );
		held_delay = delay;
		first = false;
		frames++;

	}

	void writer::encode ( int32_t left, int32_t top, int32_t right, int32_t bottom ) {

		static uint32_t counts [ bins ];
		static uint32_t sums [ bins ] [ 3 ];
		static int16_t lookup [ bins ];
		uint32_t palette [ 255 ] = { };
		uint32_t chosen_count = 0;
		int32_t area_width = right - left;
		int32_t area_height = bottom - top;

		for ( uint32_t i = 0; i < bins; i++ ) {

			counts [ i ] = 0;
			sums [ i ] [ 0 ] = sums [ i ] [ 1 ] = sums [ i ] [ 2 ] = 0;
			lookup [ i ] = -1;

		}

		for ( int32_t y = top; y < bottom; y++ ) {

			for ( int32_t x = left; x < right; x++ ) {

				size_t at = static_cast < size_t > ( y ) * width + x;
				uint32_t color = current [ at ];

				if ( !first && color == shown [ at ] )
					continue;

				uint32_t bin = bin_of ( color );

				counts [ bin ]++;
				sums [ bin ] [ 0 ] += color >> 16 & 255;
				sums [ bin ] [ 1 ] += color >> 8 & 255;
				sums [ bin ] [ 2 ] += color & 255;

			}

		}

		static uint32_t occupied [ bins ];
		static uint32_t sorted [ bins ];
		uint32_t occupied_count = 0;

		for ( uint32_t bin = 0; bin < bins; bin++ ) {

			if ( counts [ bin ] )
				occupied [ occupied_count++ ] = bin;

		}

		struct box {

			uint32_t start;
			uint32_t end;

		};

		box boxes [ 255 ];
		uint32_t box_count = occupied_count ? 1 : 0;

		boxes [ 0 ] = { 0, occupied_count };

		auto channel = [ ] ( uint32_t bin, int32_t axis ) {

			return bin >> ( 10 - axis * 5 ) & 31;

		};

		auto measure = [ & ] ( const box& item, int32_t& axis, uint64_t& weight ) {

			uint32_t low [ 3 ] = { 31, 31, 31 };
			uint32_t high [ 3 ] = { 0, 0, 0 };
			uint64_t total = 0;

			for ( uint32_t i = item.start; i < item.end; i++ ) {

				for ( int32_t c = 0; c < 3; c++ ) {

					low [ c ] = base::min ( low [ c ], channel ( occupied [ i ], c ) );
					high [ c ] = base::max ( high [ c ], channel ( occupied [ i ], c ) );

				}

				total += counts [ occupied [ i ] ];

			}

			axis = 0;

			for ( int32_t c = 1; c < 3; c++ ) {

				if ( high [ c ] - low [ c ] > high [ axis ] - low [ axis ] )
					axis = c;

			}

			weight = item.end - item.start > 1 ? total * ( high [ axis ] - low [ axis ] + 1 ) : 0;

		};

		while ( box_count < 255 ) {

			uint32_t best = 0;
			int32_t best_axis = 0;
			uint64_t best_weight = 0;

			for ( uint32_t i = 0; i < box_count; i++ ) {

				int32_t axis = 0;
				uint64_t weight = 0;

				measure ( boxes [ i ], axis, weight );

				if ( weight > best_weight ) {

					best = i;
					best_axis = axis;
					best_weight = weight;

				}

			}

			if ( !best_weight )
				break;

			box& item = boxes [ best ];
			uint32_t buckets [ 33 ] = { };
			uint64_t total = 0;

			for ( uint32_t i = item.start; i < item.end; i++ ) {

				buckets [ channel ( occupied [ i ], best_axis ) + 1 ]++;
				total += counts [ occupied [ i ] ];

			}

			for ( int32_t c = 1; c < 33; c++ )
				buckets [ c ] += buckets [ c - 1 ];

			for ( uint32_t i = item.start; i < item.end; i++ )
				sorted [ item.start + buckets [ channel ( occupied [ i ], best_axis ) ]++ ] = occupied [ i ];

			for ( uint32_t i = item.start; i < item.end; i++ )
				occupied [ i ] = sorted [ i ];

			uint64_t running = 0;
			uint32_t split = item.start + 1;

			for ( uint32_t i = item.start; i < item.end - 1; i++ ) {

				running += counts [ occupied [ i ] ];
				split = i + 1;

				if ( running * 2 >= total )
					break;

			}

			boxes [ box_count++ ] = { split, item.end };
			item.end = split;

		}

		for ( uint32_t b = 0; b < box_count; b++ ) {

			uint64_t red = 0;
			uint64_t green = 0;
			uint64_t blue = 0;
			uint64_t total = 0;

			for ( uint32_t i = boxes [ b ].start; i < boxes [ b ].end; i++ ) {

				uint32_t bin = occupied [ i ];

				red += sums [ bin ] [ 0 ];
				green += sums [ bin ] [ 1 ];
				blue += sums [ bin ] [ 2 ];
				total += counts [ bin ];
				lookup [ bin ] = static_cast < int16_t > ( b );

			}

			palette [ b ] = total ? static_cast < uint32_t > ( red / total ) << 16 | static_cast < uint32_t > ( green / total ) << 8 | static_cast < uint32_t > ( blue / total ) : 0;

		}

		chosen_count = box_count;

		base::list < uint8_t > indices;

		indices.resize ( static_cast < size_t > ( area_width ) * area_height );

		for ( int32_t y = top; y < bottom; y++ ) {

			for ( int32_t x = left; x < right; x++ ) {

				size_t at = static_cast < size_t > ( y ) * width + x;
				uint32_t color = current [ at ];
				size_t out = static_cast < size_t > ( y - top ) * area_width + ( x - left );

				if ( !first && color == shown [ at ] ) {

					indices [ out ] = transparent;

					continue;

				}

				uint32_t bin = bin_of ( color );

				if ( lookup [ bin ] < 0 ) {

					int32_t red = static_cast < int32_t > ( color >> 16 & 255 );
					int32_t green = static_cast < int32_t > ( color >> 8 & 255 );
					int32_t blue = static_cast < int32_t > ( color & 255 );
					int32_t best = 0;
					int32_t distance = 0x7FFFFFFF;

					for ( uint32_t i = 0; i < chosen_count; i++ ) {

						int32_t dr = red - static_cast < int32_t > ( palette [ i ] >> 16 & 255 );
						int32_t dg = green - static_cast < int32_t > ( palette [ i ] >> 8 & 255 );
						int32_t db = blue - static_cast < int32_t > ( palette [ i ] & 255 );
						int32_t score = dr * dr * 3 + dg * dg * 4 + db * db * 2;

						if ( score < distance ) {

							distance = score;
							best = static_cast < int32_t > ( i );

						}

					}

					lookup [ bin ] = static_cast < int16_t > ( best );

				}

				indices [ out ] = static_cast < uint8_t > ( lookup [ bin ] );
				shown [ at ] = color;

			}

		}

		held.clear ( );

		uint8_t control [ 8 ] = { 0x21, 0xF9, 0x04, 0x05, 0, 0, transparent, 0 };
		uint8_t descriptor [ 10 ] = { 0x2C, static_cast < uint8_t > ( left ), static_cast < uint8_t > ( left >> 8 ), static_cast < uint8_t > ( top ), static_cast < uint8_t > ( top >> 8 ), static_cast < uint8_t > ( area_width ), static_cast < uint8_t > ( area_width >> 8 ), static_cast < uint8_t > ( area_height ), static_cast < uint8_t > ( area_height >> 8 ), 0x87 };

		for ( uint8_t value : control )
			held.push ( value );

		held_delay_at = 4;

		for ( uint8_t value : descriptor )
			held.push ( value );

		for ( uint32_t i = 0; i < 256; i++ ) {

			uint32_t color = i < chosen_count ? palette [ i ] : 0;

			held.push ( static_cast < uint8_t > ( color >> 16 ) );
			held.push ( static_cast < uint8_t > ( color >> 8 ) );
			held.push ( static_cast < uint8_t > ( color ) );

		}

		compress ( indices.data ( ), static_cast < uint32_t > ( indices.size ( ) ), held );

	}

	void writer::flush ( ) {

		if ( held.empty ( ) )
			return;

		uint32_t delay = base::max ( held_delay, 2u );

		held [ held_delay_at ] = static_cast < uint8_t > ( delay > 0xFFFF ? 0xFF : delay );
		held [ held_delay_at + 1 ] = static_cast < uint8_t > ( delay > 0xFFFF ? 0xFF : delay >> 8 );
		put ( held.data ( ), static_cast < uint32_t > ( held.size ( ) ) );
		held.clear ( );

	}

	bool writer::close ( ) {

		if ( !file )
			return false;

		flush ( );

		uint8_t trailer = 0x3B;

		put ( &trailer, 1 );
		CloseHandle ( static_cast < HANDLE > ( file ) );
		file = nullptr;
		runtime_release ( current );
		runtime_release ( shown );
		current = nullptr;
		shown = nullptr;

		return !failed && frames > 0;

	}

}