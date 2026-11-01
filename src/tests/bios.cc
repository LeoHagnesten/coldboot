#ifdef COLDBOOT_DEBUG

#include "assembler/assembler.hh"
#include "firmware/firmware.hh"
#include "machine/xt.hh"
#include "storage/program.hh"
#include "tests/harness.hh"

namespace tests {

	static const char* const pixel_program =
		"org 0x7C00\n"
		"mov ax, 0x0004\n"
		"int 0x10\n"
		"mov ax, 0x0C03\n"
		"xor bh, bh\n"
		"mov cx, 5\n"
		"mov dx, 1\n"
		"int 0x10\n"
		"park: hlt\n"
		"jmp park\n";

	static const char* const boot_program =
		"org 0x7C00\n"
		"mov ax, 0x0E4F\n"
		"int 0x10\n"
		"mov ax, 0x0E4B\n"
		"int 0x10\n"
		"xor ax, ax\n"
		"mov ds, ax\n"
		"mov es, ax\n"
		"mov ax, 0x0201\n"
		"mov bx, 0x0800\n"
		"mov cx, 0x0002\n"
		"xor dh, dh\n"
		"int 0x13\n"
		"mov [0x0600], ax\n"
		"mov ah, 0x08\n"
		"int 0x13\n"
		"mov [0x0604], cx\n"
		"int 0x12\n"
		"mov [0x0606], ax\n"
		"xor ah, ah\n"
		"int 0x16\n"
		"mov [0x0602], ax\n"
		"park: hlt\n"
		"jmp park\n"
		"times 510 - ( $ - $$ ) db 0\n"
		"dw 0xAA55\n";

	void bios_tests ( ) {

		print ( "\r\n" );

		base::list < uint8_t > rom;
		char message [ 200 ] = { };

		expect ( firmware::build ( rom, message, sizeof ( message ), nullptr ) && rom.count ( ) == static_cast < int32_t > ( firmware::rom_size ), "BIOS: the firmware source assembles to a 16 KB ROM" );

		uint8_t sum = 0;

		for ( uint8_t value : rom )
			sum = static_cast < uint8_t > ( sum + value );

		expect ( sum == 0 && rom [ 0x3FFE ] == 0xFE && rom [ 0x3FF0 ] == 0xEA, "BIOS: checksum is zero, model byte is FE and FFFF:0 jumps to POST" );

		assembler::result* sector = new assembler::result ( );
		bool assembled = assembler::assemble ( boot_program, base::text_length ( boot_program ), nullptr, 0, *sector ) && sector->bytes.count ( ) == 512;

		expect ( assembled, "BIOS: the test boot sector assembles" );

		base::list < uint8_t > image;

		image.fill ( 368640, 0 );

		for ( int32_t i = 0; assembled && i < 512; i++ )
			image [ i ] = sector->bytes [ i ];

		for ( int32_t i = 0; i < 512; i++ )
			image [ 512 + i ] = static_cast < uint8_t > ( i * 3 + 1 );

		delete sector;

		machine::xt* board = new machine::xt ( );

		board->load_builtin ( message, sizeof ( message ), nullptr );
		board->floppy.insert ( 0, image.data ( ), static_cast < uint32_t > ( image.count ( ) ), false );
		board->power_on ( );
		board->run ( machine::xt::clock_rate * 10 );

		bool printed = false;

		for ( uint32_t at = 0xB8000; at < 0xB8000 + 4000 && !printed; at += 2 )
			printed = board->memory.read8 ( at ) == 'O' && board->memory.read8 ( at + 2 ) == 'K';

		bool loaded = true;

		for ( int32_t i = 0; i < 512 && loaded; i++ )
			loaded = board->memory.read8 ( 0x0800 + static_cast < uint32_t > ( i ) ) == static_cast < uint8_t > ( i * 3 + 1 );

		expect ( board->memory.read8 ( 0x0400 + 0x13 ) == 0x80 && board->memory.read8 ( 0x0400 + 0x14 ) == 0x02, "BIOS: POST sizes memory at 640 KB" );
		expect ( printed, "BIOS: the boot sector prints through INT 10h teletype" );
		expect ( board->memory.read16 ( 0x0600 ) == 0x0001 && loaded, "BIOS: INT 13h reads a sector through the 765 and DMA" );
		expect ( board->memory.read16 ( 0x0604 ) == 0x4F12 && board->memory.read16 ( 0x0606 ) == 640, "BIOS: INT 13h AH=08 and INT 12h report the drive and memory" );

		base::list < uint8_t > snapshot;

		board->save_state ( snapshot );
		board->keys.press ( 0x2A );
		board->run ( machine::xt::clock_rate / 20 );
		board->keys.press ( 0x1E );
		board->run ( machine::xt::clock_rate / 20 );
		board->keys.press ( 0x9E );
		board->keys.press ( 0xAA );
		board->run ( machine::xt::clock_rate / 10 );

		expect ( board->memory.read16 ( 0x0602 ) == 0x1E41, "BIOS: INT 09h and INT 16h turn Shift+A into 1E41" );

		machine::xt* copy = new machine::xt ( );
		bool restored = copy->load_state ( snapshot.data ( ), static_cast < uint32_t > ( snapshot.count ( ) ) );

		expect ( restored && copy->memory.read16 ( 0x0602 ) == 0 && copy->memory.read16 ( 0x0600 ) == 0x0001 && copy->core.state.segments [ machine::SEG_CS ] == 0xF000, "state: a saved machine loads into a fresh board, waiting inside INT 16h" );

		copy->keys.press ( 0x30 );
		copy->run ( machine::xt::clock_rate / 20 );
		copy->keys.press ( 0xB0 );
		copy->run ( machine::xt::clock_rate / 10 );

		expect ( copy->memory.read16 ( 0x0602 ) == 0x3062 && copy->floppy.drives [ 0 ].disk.present ( ), "state: the restored machine keeps running and answers a key" );
		expect ( !board->load_state ( snapshot.data ( ), 100 ) && board->memory.read16 ( 0x0602 ) == 0x1E41, "state: a truncated state is refused and leaves the machine alone" );

		delete copy;
		delete board;

		base::list < uint8_t > disk;
		uint32_t used = 0;
		bool made = machine::make_program_disk ( pixel_program, base::text_length ( pixel_program ), disk, used, message, sizeof ( message ) );
		machine::xt* plotter = new machine::xt ( );

		plotter->load_builtin ( message, sizeof ( message ), nullptr );
		plotter->floppy.insert ( 0, disk.data ( ), static_cast < uint32_t > ( disk.count ( ) ), false );
		plotter->power_on ( );
		plotter->run ( machine::xt::clock_rate * 10 );

		expect ( made && plotter->memory.read8 ( 0x0449 ) == 4 && plotter->memory.read8 ( 0xBA001 ) == 0x30, "BIOS: an assembled program boots and plots a pixel in CGA mode 4" );

		delete plotter;

	}

}

#endif