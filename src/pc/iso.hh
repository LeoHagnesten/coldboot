#pragma once

#include "base/base.hh"

namespace pc {

	class iso_builder {

	public:

		bool add_file ( const char* path, const wchar_t* source );

		void set_label ( const char* text );

		void set_boot ( const char* path, uint16_t sectors );

		bool write ( const wchar_t* target, char* error, int32_t room );

	private:

		struct node {

			char name [ 64 ];
			int32_t parent;
			bool folder;
			wchar_t source [ 260 ];
			uint64_t size;
			uint32_t extent;
			uint32_t primary_extent;
			uint32_t joliet_extent;
			uint32_t primary_size;
			uint32_t joliet_size;
			uint16_t number;

		};

		int32_t find_child ( int32_t parent, const char* name, int32_t length ) const;

		int32_t ensure_folder ( const char* path, int32_t length );

		void children ( int32_t parent, base::list < int32_t >& out ) const;

		uint32_t directory_size ( int32_t folder, bool joliet ) const;

		void directory ( int32_t folder, bool joliet, base::list < uint8_t >& out ) const;

		void path_table ( bool joliet, bool big, base::list < uint8_t >& out ) const;

		void descriptor ( uint8_t type, bool joliet, uint32_t total, uint32_t table_size, uint32_t little, uint32_t big, uint8_t* out ) const;

		base::list < node > nodes;
		char label [ 33 ] = "COLDBOOT";
		char boot_path [ 260 ] = { };
		uint16_t boot_sectors = 4;

	};

	bool iso_read_file ( const wchar_t* image, const char* path, base::list < uint8_t >& out, uint32_t limit );

}