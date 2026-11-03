#pragma once

#include "pc/storage.hh"

namespace pc {

	class fat_reader {

	public:

		bool open ( disk_file* media, uint64_t offset, uint64_t limit );

		bool read_file ( const char* path, base::list < uint8_t >& out );

		uint64_t volume_bytes ( ) const {

			return static_cast < uint64_t > ( total_sectors ) * sector_size;

		}

	private:

		struct entry {

			uint32_t cluster = 0;
			uint32_t size = 0;
			bool folder = false;

		};

		bool read ( uint64_t offset, void* out, uint32_t count );

		uint32_t next ( uint32_t cluster );

		bool find ( entry& folder, const char* name, uint32_t length, entry& out, bool root );

		disk_file* media = nullptr;
		uint64_t start = 0;
		uint32_t sector_size = 512;
		uint32_t cluster_sectors = 1;
		uint32_t total_sectors = 0;
		uint32_t root_entries = 0;
		uint32_t root_cluster = 0;
		uint32_t cluster_count = 0;
		uint32_t bits = 12;
		uint64_t fat_start = 0;
		uint64_t root_start = 0;
		uint64_t data_start = 0;

	};

}