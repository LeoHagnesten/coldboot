#include "pc/fat.hh"

namespace pc {

	static uint16_t le16 ( const uint8_t* at ) {

		return static_cast < uint16_t > ( at [ 0 ] | at [ 1 ] << 8 );

	}

	static uint32_t le32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24;

	}

	static char upper ( char value ) {

		return value >= 'a' && value <= 'z' ? static_cast < char > ( value - 32 ) : value;

	}

	bool fat_reader::read ( uint64_t offset, void* out, uint32_t count ) {

		return media && media->read ( start + offset, out, count );

	}

	bool fat_reader::open ( disk_file* disk, uint64_t offset, uint64_t limit ) {

		media = disk;
		start = offset;

		uint8_t boot [ 512 ];

		if ( !read ( 0, boot, 512 ) || boot [ 510 ] != 0x55 || boot [ 511 ] != 0xAA )
			return false;

		sector_size = le16 ( boot + 11 );
		cluster_sectors = boot [ 13 ];

		uint32_t reserved = le16 ( boot + 14 );
		uint32_t fats = boot [ 16 ];

		root_entries = le16 ( boot + 17 );
		total_sectors = le16 ( boot + 19 ) ? le16 ( boot + 19 ) : le32 ( boot + 32 );

		uint32_t fat_size = le16 ( boot + 22 ) ? le16 ( boot + 22 ) : le32 ( boot + 36 );

		if ( ( sector_size != 512 && sector_size != 1024 && sector_size != 2048 && sector_size != 4096 ) || !cluster_sectors || !fats || !fat_size || !total_sectors )
			return false;

		if ( limit && static_cast < uint64_t > ( total_sectors ) * sector_size > limit + sector_size )
			return false;

		uint32_t root_sectors = ( root_entries * 32 + sector_size - 1 ) / sector_size;
		uint32_t data_sectors = total_sectors - reserved - fats * fat_size - root_sectors;

		cluster_count = data_sectors / cluster_sectors;
		bits = cluster_count < 4085 ? 12u : ( cluster_count < 65525 ? 16u : 32u );
		fat_start = static_cast < uint64_t > ( reserved ) * sector_size;
		root_start = fat_start + static_cast < uint64_t > ( fats ) * fat_size * sector_size;
		data_start = root_start + static_cast < uint64_t > ( root_sectors ) * sector_size;
		root_cluster = bits == 32 ? le32 ( boot + 44 ) : 0;

		return true;

	}

	uint32_t fat_reader::next ( uint32_t cluster ) {

		uint8_t bytes [ 4 ] = { };

		if ( bits == 12 ) {

			if ( !read ( fat_start + cluster + cluster / 2, bytes, 2 ) )
				return 0x0FFFFFFF;

			uint32_t value = le16 ( bytes );

			value = cluster & 1 ? value >> 4 : value & 0xFFF;

			return value >= 0xFF8 ? 0x0FFFFFFF : value;

		}

		if ( bits == 16 ) {

			if ( !read ( fat_start + cluster * 2ull, bytes, 2 ) )
				return 0x0FFFFFFF;

			uint32_t value = le16 ( bytes );

			return value >= 0xFFF8 ? 0x0FFFFFFF : value;

		}

		if ( !read ( fat_start + cluster * 4ull, bytes, 4 ) )
			return 0x0FFFFFFF;

		uint32_t value = le32 ( bytes ) & 0x0FFFFFFF;

		return value >= 0x0FFFFFF8 ? 0x0FFFFFFF : value;

	}

	bool fat_reader::find ( entry& folder, const char* name, uint32_t length, entry& out, bool root ) {

		uint32_t cluster_bytes = cluster_sectors * sector_size;
		uint32_t cluster = folder.cluster;
		uint64_t fixed_at = root_start;
		uint32_t fixed_left = root_entries;
		char long_name [ 260 ] = { };
		uint32_t long_length = 0;
		uint8_t record [ 32 ];

		for ( uint32_t guard = 0; guard < 0x100000; guard++ ) {

			uint64_t at = 0;

			if ( root && bits != 32 ) {

				if ( !fixed_left )
					return false;

				at = fixed_at;
				fixed_at += 32;
				fixed_left--;

			} else {

				uint32_t index = guard % ( cluster_bytes / 32 );

				if ( guard && !index ) {

					cluster = next ( cluster );

					if ( cluster < 2 || cluster >= 0x0FFFFFF0 )
						return false;

				}

				at = data_start + static_cast < uint64_t > ( cluster - 2 ) * cluster_bytes + index * 32ull;

			}

			if ( !read ( at, record, 32 ) || record [ 0 ] == 0 )
				return false;

			if ( record [ 0 ] == 0xE5 ) {

				long_length = 0;

				continue;

			}

			if ( record [ 11 ] == 0x0F ) {

				uint32_t order = ( record [ 0 ] & 0x1F ) - 1u;
				static const uint8_t places [ 13 ] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

				if ( record [ 0 ] & 0x40 )
					long_length = 0;

				for ( uint32_t c = 0; c < 13 && order * 13 + c < 259; c++ ) {

					uint16_t value = le16 ( record + places [ c ] );

					if ( value == 0 || value == 0xFFFF )
						break;

					long_name [ order * 13 + c ] = static_cast < char > ( value < 0x80 ? value : '?' );

					if ( order * 13 + c + 1 > long_length )
						long_length = order * 13 + c + 1;

				}

				continue;

			}

			if ( record [ 11 ] & 0x08 ) {

				long_length = 0;

				continue;

			}

			char short_name [ 13 ] = { };
			uint32_t short_length = 0;

			for ( uint32_t i = 0; i < 8 && record [ i ] != ' '; i++ )
				short_name [ short_length++ ] = static_cast < char > ( record [ i ] );

			if ( record [ 8 ] != ' ' ) {

				short_name [ short_length++ ] = '.';

				for ( uint32_t i = 8; i < 11 && record [ i ] != ' '; i++ )
					short_name [ short_length++ ] = static_cast < char > ( record [ i ] );

			}

			auto same = [ & ] ( const char* candidate, uint32_t candidate_length ) {

				if ( candidate_length != length )
					return false;

				for ( uint32_t i = 0; i < length; i++ ) {

					if ( upper ( candidate [ i ] ) != upper ( name [ i ] ) )
						return false;

				}

				return true;

			};

			bool matched = same ( short_name, short_length ) || ( long_length && same ( long_name, long_length ) );

			long_length = 0;

			if ( matched ) {

				out.cluster = static_cast < uint32_t > ( le16 ( record + 20 ) ) << 16 | le16 ( record + 26 );
				out.size = le32 ( record + 28 );
				out.folder = ( record [ 11 ] & 0x10 ) != 0;

				return true;

			}

		}

		return false;

	}

	bool fat_reader::read_file ( const char* path, base::list < uint8_t >& out ) {

		entry current;

		current.cluster = root_cluster;
		current.folder = true;

		bool root = true;

		for ( const char* at = path; *at; ) {

			while ( *at == '\\' || *at == '/' )
				at++;

			uint32_t length = 0;

			while ( at [ length ] && at [ length ] != '\\' && at [ length ] != '/' )
				length++;

			if ( !length )
				break;

			entry found;

			if ( !current.folder || !find ( current, at, length, found, root ) )
				return false;

			current = found;
			root = false;
			at += length;

		}

		if ( current.folder || current.size > 0x8000000 )
			return false;

		uint32_t cluster_bytes = cluster_sectors * sector_size;
		uint32_t cluster = current.cluster;
		uint32_t done = 0;

		out.resize ( current.size );

		while ( done < current.size ) {

			uint32_t take = current.size - done < cluster_bytes ? current.size - done : cluster_bytes;

			if ( cluster < 2 || cluster >= 0x0FFFFFF0 || !read ( data_start + static_cast < uint64_t > ( cluster - 2 ) * cluster_bytes, out.data ( ) + done, take ) )
				return false;

			done += take;
			cluster = next ( cluster );

		}

		return true;

	}

}