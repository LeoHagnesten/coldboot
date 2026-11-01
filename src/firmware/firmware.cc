#include "firmware/firmware.hh"
#include "assembler/assembler.hh"
#include "video/cga.hh"
#include <windows.h>

namespace firmware {

	struct part {

		int32_t id;
		const char* name;

	};

	static const part parts [ ] = {

		{ 101, "layout.asm" },
		{ 102, "post.asm" },
		{ 103, "video.asm" },
		{ 104, "keyboard.asm" },
		{ 105, "disk.asm" },
		{ 106, "services.asm" },
		{ 107, "fixed.asm" },

	};

	static constexpr int32_t part_count = sizeof ( parts ) / sizeof ( parts [ 0 ] );

	static void write_error ( char* out, int32_t room, const char* file, int32_t line, const char* message ) {

		if ( !out || room <= 0 )
			return;

		int32_t used = 0;

		auto add = [ & ] ( const char* text ) {

			for ( int32_t i = 0; text [ i ] && used < room - 1; i++ )
				out [ used++ ] = text [ i ];

		};

		add ( file );

		if ( line > 0 ) {

			char digits [ 12 ];
			int32_t count = 0;
			char text [ 14 ] = ":";

			do {

				digits [ count++ ] = static_cast < char > ( '0' + line % 10 );
				line /= 10;

			} while ( line );

			for ( int32_t i = 0; i < count; i++ )
				text [ 1 + i ] = digits [ count - 1 - i ];

			text [ 1 + count ] = 0;
			add ( text );

		}

		add ( ": " );
		add ( message );
		out [ used ] = 0;

	}

	static const char* resource_text ( int32_t id, int32_t& size ) {

		HMODULE module = GetModuleHandleW ( nullptr );
		HRSRC found = FindResourceW ( module, MAKEINTRESOURCEW ( id ), MAKEINTRESOURCEW ( 10 ) );

		size = 0;

		if ( !found )
			return nullptr;

		HGLOBAL loaded = LoadResource ( module, found );

		if ( !loaded )
			return nullptr;

		size = static_cast < int32_t > ( SizeofResource ( module, found ) );

		return static_cast < const char* > ( LockResource ( loaded ) );

	}

	bool build ( base::list < uint8_t >& out, char* error, int32_t error_room, int32_t* error_line ) {

		base::list < char > text;
		int32_t starts [ part_count ] = { };
		int32_t line = 1;

		if ( error_line )
			*error_line = 0;

		for ( int32_t i = 0; i < part_count; i++ ) {

			int32_t size = 0;
			const char* source = resource_text ( parts [ i ].id, size );

			if ( !source ) {

				write_error ( error, error_room, parts [ i ].name, 0, "missing from the program's resources" );

				return false;

			}

			starts [ i ] = line;

			for ( int32_t c = 0; c < size; c++ ) {

				if ( source [ c ] == '\r' )
					continue;

				text.push ( source [ c ] );

				if ( source [ c ] == '\n' )
					line++;

			}

			text.push ( '\n' );
			line++;

		}

		assembler::blob blobs [ 2 ] = {

			{ "font_low", &machine::font_8x8 [ 0 ] [ 0 ], 1024 },
			{ "font_high", &machine::font_8x8 [ 128 ] [ 0 ], 1024 },

		};

		assembler::result* image = new assembler::result ( );
		bool ok = assembler::assemble ( text.data ( ), text.count ( ), blobs, 2, *image );

		if ( !ok ) {

			int32_t which = 0;

			for ( int32_t i = 0; i < part_count; i++ ) {

				if ( image->error_line >= starts [ i ] )
					which = i;

			}

			int32_t local = image->error_line - starts [ which ] + 1;

			write_error ( error, error_room, parts [ which ].name, local, image->error );

			if ( error_line )
				*error_line = local;

			delete image;

			return false;

		}

		if ( image->origin != rom_origin || image->bytes.count ( ) != static_cast < int32_t > ( rom_size ) ) {

			write_error ( error, error_room, "fixed.asm", 0, "the BIOS has to fill F000:C000 to F000:FFFF exactly" );
			delete image;

			return false;

		}

		uint8_t sum = 0;

		for ( uint32_t i = 0; i + 1 < rom_size; i++ )
			sum = static_cast < uint8_t > ( sum + image->bytes [ i ] );

		image->bytes [ rom_size - 1 ] = static_cast < uint8_t > ( 0x100 - sum );
		out = base::move ( image->bytes );
		delete image;

		return true;

	}

}