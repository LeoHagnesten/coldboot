#pragma once

#include "base/base.hh"
#include <windows.h>

namespace app {

	enum theme_choice : uint8_t {

		THEME_SYSTEM,
		THEME_DARK,
		THEME_LIGHT,

	};

	struct settings {

		theme_choice theme = THEME_SYSTEM;
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
		int32_t manager_x = 0;
		int32_t manager_y = 0;
		int32_t manager_width = 0;
		int32_t manager_height = 0;
		bool manager_maximized = false;
		wchar_t bios [ MAX_PATH ] = { };
		wchar_t disks [ 2 ] [ MAX_PATH ] = { };

	};

	void load_settings ( settings& out );

	void save_settings ( const settings& in );

	const wchar_t* data_path ( const wchar_t* name );

	bool system_light ( );

	bool use_light ( const settings& in );

}