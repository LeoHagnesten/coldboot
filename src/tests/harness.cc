#ifdef COLDBOOT_DEBUG

#include "tests/harness.hh"
#include <windows.h>

namespace tests {

	static HANDLE output = nullptr;
	static int32_t failures = 0;

	void open_output ( ) {

		output = GetStdHandle ( STD_OUTPUT_HANDLE );

		if ( !output || output == INVALID_HANDLE_VALUE ) {

			AttachConsole ( ATTACH_PARENT_PROCESS );
			output = CreateFileW ( L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr );

		}

	}

	int32_t failure_count ( ) {

		return failures;

	}

	void print ( const char* text ) {

		DWORD written = 0;

		if ( output && output != INVALID_HANDLE_VALUE )
			WriteFile ( output, text, static_cast < DWORD > ( base::text_length ( text ) ), &written, nullptr );

	}

	void expect ( bool passed, const char* name ) {

		print ( passed ? "  pass  " : "  FAIL  " );
		print ( name );
		print ( "\r\n" );

		if ( !passed )
			failures++;

	}

	void append ( char* out, int32_t room, const char* text ) {

		int32_t length = base::text_length ( out );

		for ( int32_t i = 0; text [ i ] && length < room - 1; i++ )
			out [ length++ ] = text [ i ];

		out [ length ] = 0;

	}

	void append_hex ( char* out, int32_t room, uint32_t value, int32_t digits ) {

		char text [ 12 ];

		for ( int32_t i = 0; i < digits; i++ )
			text [ i ] = "0123456789ABCDEF" [ ( value >> ( ( digits - 1 - i ) * 4 ) ) & 15 ];

		text [ digits ] = 0;
		append ( out, room, text );

	}

	void append_number ( char* out, int32_t room, int64_t value ) {

		char text [ 24 ];
		int32_t length = 0;
		char digits [ 24 ];

		if ( value == 0 )
			digits [ length++ ] = '0';

		while ( value > 0 ) {

			digits [ length++ ] = static_cast < char > ( '0' + value % 10 );
			value /= 10;

		}

		for ( int32_t i = 0; i < length; i++ )
			text [ i ] = digits [ length - 1 - i ];

		text [ length ] = 0;
		append ( out, room, text );

	}

}

#endif