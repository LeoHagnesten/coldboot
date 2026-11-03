#pragma once

#include "base/base.hh"
#include <windows.h>

namespace vm {

	struct setup_answers {

		wchar_t user [ 64 ] = { };
		wchar_t password [ 64 ] = { };
		char language [ 16 ] = { };
		bool uefi = false;
		bool windows11 = false;

	};

	bool image_language ( const wchar_t* iso, char* out, int32_t room );

	bool user_name_valid ( const wchar_t* name );

	void default_user_name ( wchar_t* out, int32_t room );

	void build_answers ( const setup_answers& answers, base::list < char >& out );

	bool write_setup_disc ( const wchar_t* folder, const setup_answers& answers, wchar_t* out, char* error, int32_t room );

}