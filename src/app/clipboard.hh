#pragma once

#include "base/base.hh"
#include <windows.h>

namespace app {

	bool clipboard_has_files ( );

	bool clipboard_has_text ( );

	bool read_clipboard_text ( HWND owner, base::list < wchar_t >& out );

	int32_t read_clipboard_files ( HWND owner, wchar_t ( *out ) [ MAX_PATH ], int32_t room );

	DWORD write_clipboard_text ( HWND owner, const wchar_t* text );

	DWORD write_clipboard_files ( HWND owner, const wchar_t* items );

	void remove_tree ( const wchar_t* path );

}