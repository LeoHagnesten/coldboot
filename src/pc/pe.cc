#include "pc/pe.hh"

namespace pc {

	static uint16_t le16 ( const uint8_t* at ) {

		return static_cast < uint16_t > ( at [ 0 ] | at [ 1 ] << 8 );

	}

	static uint32_t le32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24;

	}

	static uint64_t le64 ( const uint8_t* at ) {

		return static_cast < uint64_t > ( le32 ( at ) ) | static_cast < uint64_t > ( le32 ( at + 4 ) ) << 32;

	}

	static bool headers ( const uint8_t* file, uint32_t size, uint32_t& header, uint32_t& optional ) {

		if ( size < 0x40 || file [ 0 ] != 'M' || file [ 1 ] != 'Z' )
			return false;

		header = le32 ( file + 0x3C );

		if ( header > size - 24 || le32 ( file + header ) != 0x00004550 )
			return false;

		optional = header + 24;

		uint32_t optional_size = le16 ( file + header + 20 );

		return optional_size >= 112 && optional + optional_size <= size && le16 ( file + optional ) == 0x20B;

	}

	bool pe_inspect ( const uint8_t* file, uint32_t size, pe_info& out ) {

		uint32_t header = 0;
		uint32_t optional = 0;

		if ( !headers ( file, size, header, optional ) )
			return false;

		out.machine = le16 ( file + header + 4 );
		out.entry = le32 ( file + optional + 16 );
		out.preferred = le64 ( file + optional + 24 );
		out.image_size = le32 ( file + optional + 56 );
		out.subsystem = le16 ( file + optional + 68 );

		return out.image_size >= le32 ( file + optional + 60 ) && out.entry < out.image_size;

	}

	bool pe_place ( const uint8_t* file, uint32_t size, uint8_t* out, uint64_t base ) {

		uint32_t header = 0;
		uint32_t optional = 0;

		if ( !headers ( file, size, header, optional ) )
			return false;

		uint32_t image_size = le32 ( file + optional + 56 );
		uint32_t header_size = le32 ( file + optional + 60 );
		uint32_t sections = le16 ( file + header + 6 );
		uint32_t table = optional + le16 ( file + header + 20 );
		uint32_t directories = le32 ( file + optional + 108 );

		if ( header_size > size || header_size > image_size || table + sections * 40 > size )
			return false;

		for ( uint32_t i = 0; i < image_size; i++ )
			out [ i ] = 0;

		for ( uint32_t i = 0; i < header_size; i++ )
			out [ i ] = file [ i ];

		for ( uint32_t s = 0; s < sections; s++ ) {

			const uint8_t* section = file + table + s * 40;
			uint32_t virtual_size = le32 ( section + 8 );
			uint32_t address = le32 ( section + 12 );
			uint32_t raw_size = le32 ( section + 16 );
			uint32_t raw_at = le32 ( section + 20 );
			uint32_t count = raw_size < virtual_size || !virtual_size ? raw_size : virtual_size;

			if ( address > image_size || count > image_size - address || ( count && ( raw_at > size || count > size - raw_at ) ) )
				return false;

			for ( uint32_t i = 0; i < count; i++ )
				out [ address + i ] = file [ raw_at + i ];

		}

		if ( directories <= 5 )
			return true;

		uint32_t relocations = le32 ( file + optional + 112 + 5 * 8 );
		uint32_t relocation_size = le32 ( file + optional + 112 + 5 * 8 + 4 );
		uint64_t delta = base - le64 ( file + optional + 24 );

		if ( !relocations || !relocation_size || !delta )
			return true;

		if ( relocations > image_size || relocation_size > image_size - relocations )
			return false;

		for ( uint32_t at = relocations; at + 8 <= relocations + relocation_size; ) {

			uint32_t page = le32 ( out + at );
			uint32_t block = le32 ( out + at + 4 );

			if ( block < 8 || at + block > relocations + relocation_size )
				break;

			for ( uint32_t item = at + 8; item + 2 <= at + block; item += 2 ) {

				uint16_t entry = le16 ( out + item );
				uint32_t where = page + ( entry & 0x0FFF );
				uint32_t kind = entry >> 12;

				if ( kind == 10 && where + 8 <= image_size ) {

					uint64_t value = le64 ( out + where ) + delta;

					for ( int32_t b = 0; b < 8; b++ )
						out [ where + b ] = static_cast < uint8_t > ( value >> ( b * 8 ) );

				} else if ( kind == 3 && where + 4 <= image_size ) {

					uint32_t value = le32 ( out + where ) + static_cast < uint32_t > ( delta );

					for ( int32_t b = 0; b < 4; b++ )
						out [ where + b ] = static_cast < uint8_t > ( value >> ( b * 8 ) );

				}

			}

			at += block;

		}

		return true;

	}

}