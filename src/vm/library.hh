#pragma once

#include "vm/config.hh"

namespace vm {

	struct entry {

		wchar_t path [ MAX_PATH ] = { };
		config settings;
		bool readable = false;
		char problem [ 120 ] = { };

	};

	class library {

	public:

		bool load ( const wchar_t* list );

		bool save ( ) const;

		bool add ( const wchar_t* path );

		void remove ( int32_t index );

		bool create ( const config& settings, const wchar_t* base_folder, wchar_t* out_path, char* error, int32_t room );

		bool refresh ( int32_t index );

		int32_t find ( const wchar_t* path ) const;

		int32_t count ( ) const {

			return entries.count ( );

		}

		entry& at ( int32_t index ) {

			return entries [ index ];

		}

		base::list < entry > entries;
		wchar_t list_path [ MAX_PATH ] = { };

	};

	bool default_folder ( wchar_t* out );

	bool folder_of ( const wchar_t* path, wchar_t* out );

	static constexpr const wchar_t* window_tag = L"ColdbootMachine";

	uint64_t running_hash ( const wchar_t* path );

	void running_name ( const wchar_t* path, wchar_t* out, int32_t room );

	bool is_running ( const wchar_t* path );

	HWND find_window ( const wchar_t* path );

	bool recycle ( const wchar_t* folder );

	bool write_preview ( const wchar_t* folder, const uint32_t* pixels, int32_t width, int32_t height );

	bool read_preview ( const wchar_t* folder, base::list < uint32_t >& pixels, int32_t& width, int32_t& height );

}