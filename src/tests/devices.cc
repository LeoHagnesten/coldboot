#ifdef COLDBOOT_DEBUG

#include "machine/xt.hh"
#include "tests/harness.hh"
#include "tests/program.hh"

namespace tests {

	static void pic_tests ( ) {

		machine::pic chip;

		chip.reset ( );
		chip.out ( 0x20, 0x13 );
		chip.out ( 0x21, 0x08 );
		chip.out ( 0x21, 0x09 );
		chip.out ( 0x21, 0x00 );
		chip.set_line ( 0, true );
		expect ( chip.pending ( ) && chip.acknowledge ( ) == 0x08 && chip.isr == 0x01 && !( chip.irr & 1 ), "8259: an edge on IRQ0 is acknowledged as vector 08" );

		chip.set_line ( 1, true );
		expect ( !chip.pending ( ), "8259: IRQ1 waits while IRQ0 is in service" );

		chip.out ( 0x20, 0x20 );
		expect ( chip.pending ( ) && chip.acknowledge ( ) == 0x09, "8259: IRQ1 follows after a non-specific EOI" );

		chip.out ( 0x20, 0x20 );
		chip.out ( 0x21, 0x04 );
		chip.set_line ( 2, true );
		expect ( !chip.pending ( ), "8259: masked lines are held back" );

		chip.out ( 0x21, 0x00 );
		chip.acknowledge ( );
		chip.out ( 0x20, 0x0B );
		expect ( chip.in ( 0x20 ) == 0x04, "8259: OCW3 selects the in-service register for reading" );

	}

	static void pit_tests ( ) {

		machine::pit chip;
		int32_t lows = 0;

		chip.reset ( );
		chip.out ( 0x43, 0x34 );
		chip.out ( 0x40, 4 );
		chip.out ( 0x40, 0 );

		for ( int32_t i = 0; i < 41; i++ ) {

			chip.clock ( );
			lows += chip.channels [ 0 ].output ? 0 : 1;

		}

		expect ( lows == 10, "8253: mode 2 pulses low once every count" );

		int32_t highs = 0;

		chip.out ( 0x43, 0x76 );
		chip.out ( 0x41, 5 );
		chip.out ( 0x41, 0 );
		chip.clock ( );

		for ( int32_t i = 0; i < 50; i++ ) {

			chip.clock ( );
			highs += chip.channels [ 1 ].output ? 1 : 0;

		}

		expect ( highs == 30, "8253: mode 3 with an odd count stays high one clock longer" );

		chip.out ( 0x43, 0x30 );
		chip.out ( 0x40, 0x10 );
		chip.out ( 0x40, 0x00 );
		expect ( !chip.channels [ 0 ].output, "8253: mode 0 drives the output low once the count is written" );

		for ( int32_t i = 0; i < 16; i++ )
			chip.clock ( );

		bool early = chip.channels [ 0 ].output;

		chip.clock ( );
		expect ( !early && chip.channels [ 0 ].output, "8253: mode 0 goes high when the count runs out" );

		chip.out ( 0x43, 0x34 );
		chip.out ( 0x40, 0x00 );
		chip.out ( 0x40, 0x10 );

		for ( int32_t i = 0; i < 6; i++ )
			chip.clock ( );

		chip.out ( 0x43, 0x00 );

		for ( int32_t i = 0; i < 9; i++ )
			chip.clock ( );

		uint8_t low = chip.in ( 0x40 );
		uint8_t high = chip.in ( 0x40 );

		expect ( low == 0xFB && high == 0x0F, "8253: a latch command freezes the count for reading" );

	}

	static void dma_tests ( ) {

		machine::dma chip;

		chip.reset ( );
		chip.out ( 0x0C, 0 );
		chip.out ( 0x00, 0x34 );
		chip.out ( 0x00, 0x12 );
		chip.out ( 0x0C, 0 );

		uint8_t low = chip.in ( 0x00 );
		uint8_t high = chip.in ( 0x00 );

		expect ( low == 0x34 && high == 0x12, "8237: address registers go through the byte flip-flop" );

		chip.out ( 0x0E, 0 );
		chip.out ( 0x0D, 0 );
		expect ( chip.masks == 0x0F, "8237: master clear masks every channel" );

		machine::bus memory;
		uint8_t value = 0x5A;

		chip.out ( 0x0E, 0 );
		chip.out ( 0x0B, 0x45 );
		chip.out ( 0x0C, 0 );
		chip.out ( 0x02, 0x00 );
		chip.out ( 0x02, 0x40 );
		chip.out ( 0x03, 0x00 );
		chip.out ( 0x03, 0x00 );
		chip.channels [ 1 ].page = 0x02;
		chip.transfer ( 1, memory, value );
		expect ( memory.read8 ( 0x24000 ) == 0x5A && ( chip.in ( 0x08 ) & 0x02 ), "8237: a write transfer lands at page:address and reports terminal count" );

	}

	static void keyboard_tests ( ) {

		machine::keyboard keys;
		machine::pit timer;
		machine::ppi chip ( keys, timer );

		keys.reset ( );
		timer.reset ( );
		chip.out ( 0x61, 0x08 );
		keys.advance ( 100000 );
		chip.out ( 0x61, 0x48 );
		keys.advance ( 10000 );
		expect ( keys.full && chip.in ( 0x60 ) == 0xAA, "keyboard: holding the clock low resets it and it answers AA" );

		chip.out ( 0x61, 0xC8 );
		chip.out ( 0x61, 0x48 );
		keys.press ( 0x1E );
		keys.advance ( 10000 );
		expect ( keys.full && chip.in ( 0x60 ) == 0x1E, "keyboard: a key arrives after the previous byte is cleared" );

		chip.out ( 0x61, 0xC8 );
		expect ( !keys.full && chip.in ( 0x60 ) == 0x00, "keyboard: PB7 empties the shift register and drops IRQ1" );

		chip.switches = 0xA5;
		chip.out ( 0x61, 0x00 );
		uint8_t low = static_cast < uint8_t > ( chip.in ( 0x62 ) & 0x0F );
		chip.out ( 0x61, 0x08 );
		uint8_t high = static_cast < uint8_t > ( chip.in ( 0x62 ) & 0x0F );
		expect ( low == 0x05 && high == 0x0A, "8255: PB3 picks which half of the switches port C shows" );

	}

	static void video_tests ( ) {

		machine::bus memory;
		machine::cga video ( memory );
		uint8_t* vram = memory.backing ( 0xB8000 );

		vram [ 0 ] = 'A';
		vram [ 1 ] = 0x1F;
		video.out ( 0x3D8, 0x29 );
		video.update ( 1000000 );

		uint32_t* row = video.pixels;

		expect ( video.width == 640 && video.height == 200, "CGA: 80x25 text renders at 640x200" );
		expect ( row [ 0 ] == 0x0000AA && row [ 2 ] == 0xFFFFFF, "CGA: text uses the attribute's foreground and background" );

		int32_t retrace = 0;
		int32_t blank = 0;

		for ( uint64_t t = 0; t < 79648; t += 16 ) {

			uint8_t status = video.status ( t );

			retrace += status & 0x08 ? 1 : 0;
			blank += status & 0x01 ? 1 : 0;

		}

		expect ( retrace > 200 && retrace < 400 && blank > 1000, "CGA: the status register shows retrace and blanking each frame" );

		vram [ 0 ] = 0x1B;
		video.out ( 0x3D8, 0x0A );
		video.out ( 0x3D9, 0x31 );
		video.update ( 4000000 );
		row = video.pixels;
		expect ( row [ 0 ] == 0x0000AA && row [ 2 ] == 0x55FFFF && row [ 4 ] == 0xFF55FF && row [ 6 ] == 0xFFFFFF, "CGA: 320x200 uses the background and the bright cyan palette" );

	}

	static void board_tests ( ) {

		machine::xt* board = new machine::xt ( );

		board->memory.write8 ( 0xB8000, 0x77 );
		expect ( board->memory.read8 ( 0xBC000 ) == 0x77 && board->memory.read8 ( 0xA0000 ) == 0xFF, "XT: video memory mirrors and memory past 640 KB reads FF" );

		program code;

		code.emit ( 0xFA ).mov_ax ( 0x0030 ).emit ( 0x8E ).emit ( 0xD0 ).emit ( 0xBC ).word ( 0x0100 );
		code.emit ( 0x31 ).emit ( 0xC0 ).emit ( 0x8E ).emit ( 0xD8 );
		code.store ( 0x0020, 0xE400 ).store ( 0x0022, 0xF000 ).store ( 0x0024, 0xE500 ).store ( 0x0026, 0xF000 ).store ( 0x0500, 0 ).store ( 0x05FE, 0 );
		code.out_byte ( 0x20, 0x13 ).out_byte ( 0x21, 0x08 ).out_byte ( 0x21, 0x09 ).out_byte ( 0x21, 0xFC );
		code.out_byte ( 0x43, 0x34 ).out_byte ( 0x40, 0xA9 ).out_byte ( 0x40, 0x04 );
		code.out_byte ( 0x43, 0xB6 ).out_byte ( 0x42, 0xA9 ).out_byte ( 0x42, 0x04 );
		code.emit ( 0xE4 ).emit ( 0x61 ).emit ( 0x0C ).emit ( 0x43 ).out ( 0x61 );

		static const uint8_t registers [ 16 ] = { 0x71, 0x50, 0x5A, 0x0A, 0x1F, 0x06, 0x19, 0x1C, 0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00 };

		for ( uint8_t i = 0; i < 16; i++ )
			code.crtc ( i, registers [ i ] );

		code.emit ( 0xBA ).word ( 0x3D8 ).mov_al ( 0x29 ).emit ( 0xEE );
		code.mov_ax ( 0xB800 ).emit ( 0x8E ).emit ( 0xC0 );
		code.emit ( 0x26 ).store ( 0x0000, 0x1F4F ).emit ( 0x26 ).store ( 0x0002, 0x1F4B );
		code.emit ( 0xFB ).emit ( 0xF4 ).emit ( 0xEB ).emit ( 0xFD );
		code.size = 0x400;
		code.emit ( 0xFF ).emit ( 0x06 ).word ( 0x0500 ).out_byte ( 0x20, 0x20 ).emit ( 0xCF );
		code.size = 0x500;
		code.emit ( 0x50 ).emit ( 0x53 ).emit ( 0xE4 ).emit ( 0x60 ).emit ( 0x8B ).emit ( 0x1E ).word ( 0x05FE ).emit ( 0x88 ).emit ( 0x87 ).word ( 0x0600 ).emit ( 0xFF ).emit ( 0x06 ).word ( 0x05FE );
		code.emit ( 0xE4 ).emit ( 0x61 ).emit ( 0x0C ).emit ( 0x80 ).out ( 0x61 ).emit ( 0x24 ).emit ( 0x7F ).out ( 0x61 ).out_byte ( 0x20, 0x20 ).emit ( 0x5B ).emit ( 0x58 ).emit ( 0xCF );
		code.size = 0x1FF0;
		code.emit ( 0xEA ).word ( 0xE000 ).word ( 0xF000 );
		code.size = 0x2000;

		expect ( board->load_bios ( code.bytes, code.size ), "XT: an 8 KB BIOS image maps at FE000" );
		board->power_on ( );
		board->run ( machine::xt::clock_rate / 10 );

		uint16_t ticks = board->memory.read16 ( 0x0500 );

		expect ( ticks >= 99 && ticks <= 101, "XT: the timer interrupts the CPU through the 8259 a thousand times a second" );
		expect ( board->video.pixels [ 0 ] == 0x0000AA && board->video.pixels [ 2 ] == 0xFFFFFF, "XT: text written by the program shows up on the CGA screen" );

		float samples [ 8192 ];
		int32_t count = board->take_samples ( samples, 8192 );
		int32_t edges = 0;

		for ( int32_t i = 1; i < count; i++ )
			edges += ( samples [ i ] > 0.0f ) != ( samples [ i - 1 ] > 0.0f ) ? 1 : 0;

		expect ( count > 4700 && count < 4900 && edges >= 196 && edges <= 202, "XT: timer channel 2 drives the speaker as a 1 kHz square wave" );

		board->keys.press ( 0x23 );
		board->run ( machine::xt::clock_rate / 100 );
		board->keys.press ( 0xA3 );
		board->run ( machine::xt::clock_rate / 100 );
		board->keys.press ( 0x12 );
		board->keys.press ( 0x92 );
		board->run ( machine::xt::clock_rate / 50 );

		uint16_t received = board->memory.read16 ( 0x05FE );
		bool in_order = board->memory.read8 ( 0x600 ) == 0x23 && board->memory.read8 ( 0x601 ) == 0xA3 && board->memory.read8 ( 0x602 ) == 0x12 && board->memory.read8 ( 0x603 ) == 0x92;

		expect ( received == 4 && in_order, "XT: each key byte reaches the IRQ1 handler exactly once, in order" );

		delete board;

	}

	void device_tests ( ) {

		print ( "\r\n" );
		pic_tests ( );
		pit_tests ( );
		dma_tests ( );
		keyboard_tests ( );
		video_tests ( );
		board_tests ( );

	}

}

#endif