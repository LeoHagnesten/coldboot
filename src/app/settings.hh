#pragma once

#include "base/base.hh"
#include <windows.h>

namespace app {

	struct settings {

		bool light = false;
		bool turbo = false;
		bool crt = false;
		bool fast_floppy = false;
		bool custom_bios = false;
		bool maximized = false;
		uint32_t memory = 640;
		int32_t window_x = 0;
		int32_t window_y = 0;
		int32_t window_width = 0;
		int32_t window_height = 0;
		wchar_t bios [ MAX_PATH ] = { };
		wchar_t disks [ 2 ] [ MAX_PATH ] = { };

	};

	void load_settings ( settings& out );

	void save_settings ( const settings& in );

	const wchar_t* data_path ( const wchar_t* name );

}