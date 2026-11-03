#pragma once

#include "base/base.hh"
#include <windows.h>

namespace app {

	enum file_kind : uint8_t {

		FILE_DISK,
		FILE_ROM,
		FILE_STATE,
		FILE_MACHINE,
		FILE_IMAGE,
		FILE_HARD_DISK,

	};

	bool open_file ( HWND owner, file_kind kind, wchar_t* out );

	int32_t open_files ( HWND owner, wchar_t ( *out ) [ MAX_PATH ], int32_t room );

	bool save_file ( HWND owner, file_kind kind, const wchar_t* suggested, wchar_t* out );

	bool save_screenshot ( const uint32_t* pixels, int32_t width, int32_t height, const wchar_t* path );

	bool screenshot_path ( wchar_t* out );

}