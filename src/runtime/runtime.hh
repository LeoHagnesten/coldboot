#pragma once

typedef __SIZE_TYPE__ runtime_size;

extern "C" {

	void* runtime_allocate ( runtime_size size );

	void* runtime_allocate_zero ( runtime_size size );

	void runtime_release ( void* block );

	void runtime_initialize ( );

	void runtime_finalize ( );

	void runtime_exit ( int code );

	int __cdecl atexit ( void ( __cdecl* callback ) ( ) );

}

#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE

inline void* operator new ( runtime_size, void* place ) noexcept {

	return place;

}

inline void operator delete ( void*, void* ) noexcept {

}

#endif

int program_main ( );