#include "runtime/runtime.hh"

extern "C" {

	__declspec ( dllimport ) void* __stdcall GetProcessHeap ( );

	__declspec ( dllimport ) void* __stdcall HeapAlloc ( void* heap, unsigned long flags, runtime_size bytes );

	__declspec ( dllimport ) int __stdcall HeapFree ( void* heap, unsigned long flags, void* block );

}

#if defined ( __x86_64__ )

extern "C" void* memset ( void* target, int value, runtime_size count ) {

	void* result = target;

	__asm__ volatile ( "rep stosb" : "+D" ( target ), "+c" ( count ) : "a" ( value ) : "memory" );

	return result;

}

extern "C" void* memcpy ( void* target, const void* source, runtime_size count ) {

	void* result = target;

	__asm__ volatile ( "rep movsb" : "+D" ( target ), "+S" ( source ), "+c" ( count ) : : "memory" );

	return result;

}

#else

extern "C" void* memset ( void* target, int value, runtime_size count ) {

	unsigned char* to = static_cast < unsigned char* > ( target );
	unsigned long long pattern = static_cast < unsigned char > ( value ) * 0x0101010101010101ull;
	runtime_size i = 0;

	for ( ; i + 8 <= count; i += 8 )
		__builtin_memcpy_inline ( to + i, &pattern, 8 );

	for ( ; i < count; i++ )
		to [ i ] = static_cast < unsigned char > ( value );

	return target;

}

extern "C" void* memcpy ( void* target, const void* source, runtime_size count ) {

	unsigned char* to = static_cast < unsigned char* > ( target );
	const unsigned char* from = static_cast < const unsigned char* > ( source );
	runtime_size i = 0;

	for ( ; i + 8 <= count; i += 8 )
		__builtin_memcpy_inline ( to + i, from + i, 8 );

	for ( ; i < count; i++ )
		to [ i ] = from [ i ];

	return target;

}

#endif

extern "C" void* memmove ( void* target, const void* source, runtime_size count ) {

	unsigned char* to = static_cast < unsigned char* > ( target );
	const unsigned char* from = static_cast < const unsigned char* > ( source );

	if ( to <= from || to >= from + count )
		return memcpy ( target, source, count );

	for ( runtime_size i = count; i > 0; i-- )
		to [ i - 1 ] = from [ i - 1 ];

	return target;

}

extern "C" int memcmp ( const void* left, const void* right, runtime_size count ) {

	const unsigned char* a = static_cast < const unsigned char* > ( left );
	const unsigned char* b = static_cast < const unsigned char* > ( right );

	for ( runtime_size i = 0; i < count; i++ ) {

		if ( a [ i ] != b [ i ] )
			return a [ i ] < b [ i ] ? -1 : 1;

	}

	return 0;

}

extern "C" runtime_size strlen ( const char* text ) {

	runtime_size length = 0;

	while ( text [ length ] )
		length++;

	return length;

}

extern "C" runtime_size wcslen ( const wchar_t* text ) {

	runtime_size length = 0;

	while ( text [ length ] )
		length++;

	return length;

}

extern "C" void* runtime_allocate ( runtime_size size ) {

	return HeapAlloc ( GetProcessHeap ( ), 0, size ? size : 1 );

}

extern "C" void* runtime_allocate_zero ( runtime_size size ) {

	return HeapAlloc ( GetProcessHeap ( ), 8, size ? size : 1 );

}

extern "C" void runtime_release ( void* block ) {

	if ( block )
		HeapFree ( GetProcessHeap ( ), 0, block );

}

void* operator new ( runtime_size size ) {

	return runtime_allocate_zero ( size );

}

void* operator new [ ] ( runtime_size size ) {

	return runtime_allocate_zero ( size );

}

void operator delete ( void* block ) noexcept {

	runtime_release ( block );

}

void operator delete ( void* block, runtime_size ) noexcept {

	runtime_release ( block );

}

void operator delete [ ] ( void* block ) noexcept {

	runtime_release ( block );

}

void operator delete [ ] ( void* block, runtime_size ) noexcept {

	runtime_release ( block );

}