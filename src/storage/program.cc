#include "storage/program.hh"
#include "assembler/assembler.hh"

namespace machine {

	static void write_error ( char* out, int32_t room, int32_t line, const char* message ) {

		if ( !out || room <= 0 )
			return;

		int32_t used = 0;

		auto add = [ & ] ( const char* text ) {

			for ( int32_t i = 0; text [ i ] && used < room - 1; i++ )
				out [ used++ ] = text [ i ];

		};

		if ( line > 0 ) {

			char digits [ 12 ];
			int32_t count = 0;
			char text [ 20 ] = "line ";

			do {

				digits [ count++ ] = static_cast < char > ( '0' + line % 10 );
				line /= 10;

			} while ( line );

			for ( int32_t i = 0; i < count; i++ )
				text [ 5 + i ] = digits [ count - 1 - i ];

			text [ 5 + count ] = 0;
			add ( text );
			add ( ": " );

		}

		add ( message );
		out [ used ] = 0;

	}

	bool is_program_path ( const wchar_t* path ) {

		int32_t length = path ? base::text_length ( path ) : 0;

		if ( length < 4 || path [ length - 4 ] != L'.' )
			return false;

		const wchar_t* tail = path + length - 3;

		return ( tail [ 0 ] | 0x20 ) == L'a' && ( tail [ 1 ] | 0x20 ) == L's' && ( tail [ 2 ] | 0x20 ) == L'm';

	}

	bool make_program_disk ( const char* text, int32_t length, base::list < uint8_t >& out, uint32_t& used, char* error, int32_t room ) {

		base::list < char > source;

		source.reserve ( static_cast < size_t > ( length ) + 1 );

		for ( int32_t i = 0; i < length; i++ ) {

			if ( text [ i ] != '\r' )
				source.push ( text [ i ] );

		}

		source.push ( '\n' );

		assembler::result* program = new assembler::result ( );

		if ( !assembler::assemble ( source.data ( ), source.count ( ), nullptr, 0, *program ) ) {

			write_error ( error, room, program->error_line, program->error );
			delete program;

			return false;

		}

		uint32_t size = static_cast < uint32_t > ( program->bytes.count ( ) );

		if ( !size || size > program_disk_size ) {

			write_error ( error, room, 0, size ? "the program doesn't fit on a 360 KB disk" : "there's no code in it" );
			delete program;

			return false;

		}

		out.fill ( program_disk_size, 0 );

		for ( uint32_t i = 0; i < size; i++ )
			out [ i ] = program->bytes [ i ];

		if ( size <= 510 ) {

			out [ 510 ] = 0x55;
			out [ 511 ] = 0xAA;

		}

		used = size;
		delete program;

		return true;

	}

}