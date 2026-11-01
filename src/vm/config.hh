#pragma once

#include "base/base.hh"
#include <windows.h>

namespace vm {

	static constexpr int32_t config_version = 1;
	static constexpr int32_t name_room = 64;
	static constexpr int32_t notes_room = 512;

	struct preset {

		const char* id;
		const wchar_t* label;
		const wchar_t* detail;
		const wchar_t* mark;
		uint32_t memory;

	};

	const preset* presets ( int32_t& count );

	const preset* find_preset ( const char* id );

	struct config {

		wchar_t name [ name_room ] = { };
		char os [ 24 ] = "pcxt";
		uint32_t memory = 640;
		bool turbo = false;
		bool crt = false;
		bool fast_floppy = false;
		bool custom_bios = false;
		bool maximized = false;
		int32_t window_x = 0;
		int32_t window_y = 0;
		int32_t window_width = 0;
		int32_t window_height = 0;
		wchar_t bios [ MAX_PATH ] = { };
		wchar_t floppies [ 2 ] [ MAX_PATH ] = { };
		wchar_t notes [ notes_room ] = { };

	};

	bool valid_name ( const wchar_t* name );

	bool parse ( const char* text, int32_t length, config& out, char* error, int32_t room );

	void serialize ( const config& in, base::list < char >& out );

	bool load ( const wchar_t* path, config& out, char* error, int32_t room );

	bool save ( const wchar_t* path, const config& in );

}