#ifdef COLDBOOT_DEBUG

#include "assembler/assembler.hh"
#include "tests/harness.hh"

namespace tests {

	static bool assembles_to ( const char* source, const uint8_t* expected, int32_t count ) {

		assembler::result* image = new assembler::result ( );
		bool ok = assembler::assemble ( source, base::text_length ( source ), nullptr, 0, *image ) && image->bytes.count ( ) == count;

		for ( int32_t i = 0; ok && i < count; i++ )
			ok = image->bytes [ i ] == expected [ i ];

		if ( !ok && image->error [ 0 ] ) {

			print ( "        " );
			print ( image->error );
			print ( "\r\n" );

		}

		delete image;

		return ok;

	}

	void assembler_tests ( ) {

		print ( "\r\n" );

		{

			static const uint8_t bytes [ ] = { 0xB8, 0x34, 0x12, 0x8A, 0x00, 0x89, 0x4E, 0xFE, 0x26, 0xC7, 0x85, 0x00, 0x01, 0x05, 0x00, 0x2E, 0x8B, 0x04, 0xA2, 0x00, 0x05, 0x8D, 0x71, 0x08 };

			expect ( assembles_to ( "mov ax, 0x1234\nmov al, [bx+si]\nmov [bp-2], cx\nmov word [es:di+0x100], 5\nmov ax, cs:[si]\nmov [0500h], al\nlea si, [bx+di+8]", bytes, sizeof ( bytes ) ), "assembler: mov and lea forms, displacements and segment overrides" );

		}

		{

			static const uint8_t bytes [ ] = { 0x05, 0x05, 0x00, 0x83, 0xC3, 0x05, 0x81, 0xC3, 0x2C, 0x01, 0x80, 0x3E, 0x49, 0x04, 0x03, 0xF6, 0x07, 0x80, 0xFF, 0x04, 0x93, 0x32, 0xE4 };

			expect ( assembles_to ( "add ax, 5\nadd bx, 5\nadd bx, 300\ncmp byte [0x449], 3\ntest byte [bx], 0x80\ninc word [si]\nxchg ax, bx\nxor ah, ah", bytes, sizeof ( bytes ) ), "assembler: arithmetic picks the accumulator and sign-extended byte forms" );

		}

		{

			static const uint8_t bytes [ ] = { 0xEC, 0xE6, 0x20, 0xF3, 0xA5, 0xD1, 0xE0, 0xD1, 0xE0, 0xD1, 0xE0, 0xD1, 0xE0, 0x06, 0x1F, 0xFF, 0x1F, 0xEA, 0x5B, 0xE0, 0x00, 0xF0, 0xCD, 0x10, 0xEB, 0xFE };

			expect ( assembles_to ( "in al, dx\nout 20h, al\nrep movsw\nshl ax, 4\npush es\npop ds\ncall far [bx]\njmp 0F000h:0E05Bh\nint 10h\nhere: jmp here", bytes, sizeof ( bytes ) ), "assembler: ports, string prefixes, shifts, far jumps and interrupts" );

		}

		{

			uint8_t bytes [ 206 ] = { 0x75, 0x03, 0xE9, 0xC8, 0x00 };

			bytes [ 205 ] = 0xC3;
			expect ( assembles_to ( "start: jz target\ntimes 200 db 0\ntarget: ret", bytes, sizeof ( bytes ) ), "assembler: a conditional jump that can't reach becomes a jump around a near jump" );

		}

		{

			static const uint8_t bytes [ ] = { 0x49, 0x75, 0xFD, 0xB0, 0x07, 0xEB, 0xFC };

			expect ( assembles_to ( "value equ 3 + 4\nfirst:\n.loop: dec cx\njnz .loop\nsecond:\n.loop: mov al, value\njmp .loop", bytes, sizeof ( bytes ) ), "assembler: local labels belong to the label above them and equ takes expressions" );

		}

		{

			static const uint8_t bytes [ ] = { 0x01, 0xFF, 0xFF, 0xFF, 0x02, 'O', 'K', 0x34, 0x12 };

			expect ( assembles_to ( "org 0x100\ndb 1\norg 0x104\ndb 2, \"OK\"\ndw 1234h", bytes, sizeof ( bytes ) ), "assembler: org pads with FF and db takes strings" );

		}

		assembler::result* image = new assembler::result ( );
		const char* broken = "mov ax, 1\nmov cs, ax\n";

		expect ( !assembler::assemble ( broken, base::text_length ( broken ), nullptr, 0, *image ) && image->error_line == 2, "assembler: errors name the line they're on" );

		const char* unknown = "nop\nnop\njmp nowhere\n";

		expect ( !assembler::assemble ( unknown, base::text_length ( unknown ), nullptr, 0, *image ) && image->error_line == 3, "assembler: unknown symbols are reported" );

		delete image;

	}

}

#endif