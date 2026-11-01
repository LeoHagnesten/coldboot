#include "runtime/runtime.hh"

extern "C" {

	__declspec ( dllimport ) __declspec ( noreturn ) void __stdcall ExitProcess ( unsigned int code );

}

typedef void ( __cdecl* runtime_callback ) ( );

extern "C" int _fltused = 0;

#pragma section ( ".CRT$XCA", long, read )
#pragma section ( ".CRT$XCZ", long, read )

extern "C" __declspec ( allocate ( ".CRT$XCA" ) ) runtime_callback runtime_initializers_begin [ ] = { nullptr };
extern "C" __declspec ( allocate ( ".CRT$XCZ" ) ) runtime_callback runtime_initializers_end [ ] = { nullptr };

static runtime_callback exit_callbacks [ 64 ];
static int exit_callback_count = 0;

extern "C" int __cdecl atexit ( runtime_callback callback ) {

	if ( exit_callback_count >= 64 )
		return 1;

	exit_callbacks [ exit_callback_count++ ] = callback;

	return 0;

}

extern "C" int __cdecl _purecall ( ) {

	ExitProcess ( 3 );

}

extern "C" void runtime_initialize ( ) {

	for ( runtime_callback* entry = runtime_initializers_begin + 1; entry < runtime_initializers_end; entry++ ) {

		if ( *entry )
			( *entry ) ( );

	}

}

extern "C" void runtime_finalize ( ) {

	while ( exit_callback_count > 0 )
		exit_callbacks [ --exit_callback_count ] ( );

}

extern "C" void runtime_exit ( int code ) {

	runtime_finalize ( );
	ExitProcess ( static_cast < unsigned int > ( code ) );

}