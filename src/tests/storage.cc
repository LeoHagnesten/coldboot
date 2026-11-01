#ifdef COLDBOOT_DEBUG

#include "machine/xt.hh"
#include "storage/blank.hh"
#include "storage/program.hh"
#include "tests/harness.hh"
#include "tests/program.hh"

namespace tests {

	struct floppy_rig {

		machine::bus memory;
		machine::dma transfers;
		machine::fdc controller;
		uint8_t results [ 7 ] = { };

		floppy_rig ( ) : controller ( transfers, memory ) {

			transfers.reset ( );
			controller.reset ( );

		}

		bool send ( const uint8_t* bytes, int32_t count ) {

			for ( int32_t i = 0; i < count; i++ ) {

				if ( ( controller.in ( 0x3F4 ) & 0xC0 ) != 0x80 )
					return false;

				controller.out ( 0x3F5, bytes [ i ] );

			}

			return true;

		}

		int32_t receive ( ) {

			int32_t count = 0;

			while ( count < 7 && ( controller.in ( 0x3F4 ) & 0xC0 ) == 0xC0 )
				results [ count++ ] = controller.in ( 0x3F5 );

			return count;

		}

		bool settle ( uint32_t limit ) {

			for ( uint32_t spent = 0; spent < limit; spent += 64 ) {

				if ( controller.interrupt ( ) )
					return true;

				controller.advance ( 64 );

			}

			return controller.interrupt ( );

		}

		void channel ( uint8_t mode, uint32_t address, uint16_t length ) {

			transfers.out ( 0x0A, 0x06 );
			transfers.out ( 0x0C, 0x00 );
			transfers.out ( 0x0B, mode );
			transfers.out ( 0x04, static_cast < uint8_t > ( address ) );
			transfers.out ( 0x04, static_cast < uint8_t > ( address >> 8 ) );
			transfers.channels [ 2 ].page = static_cast < uint8_t > ( address >> 16 );
			transfers.out ( 0x05, static_cast < uint8_t > ( length - 1 ) );
			transfers.out ( 0x05, static_cast < uint8_t > ( ( length - 1 ) >> 8 ) );
			transfers.out ( 0x0A, 0x02 );

		}

		bool sense ( uint8_t& status, uint8_t& cylinder ) {

			static const uint8_t command [ ] = { 0x08 };

			if ( !send ( command, 1 ) || receive ( ) != 2 )
				return false;

			status = results [ 0 ];
			cylinder = results [ 1 ];

			return true;

		}

	};

	static uint8_t pattern ( uint32_t cylinder, uint32_t head, uint32_t record, uint32_t offset ) {

		return static_cast < uint8_t > ( cylinder * 31 + head * 17 + record * 7 + offset );

	}

	static base::list < uint8_t > make_image ( const machine::floppy_format& format ) {

		base::list < uint8_t > image;

		image.resize ( format.size );

		for ( uint32_t c = 0; c < format.cylinders; c++ ) {

			for ( uint32_t h = 0; h < format.heads; h++ ) {

				for ( uint32_t r = 1; r <= format.sectors; r++ ) {

					uint32_t base = ( ( c * format.heads + h ) * format.sectors + r - 1 ) * machine::floppy::sector_size;

					for ( uint32_t i = 0; i < machine::floppy::sector_size; i++ )
						image [ base + i ] = pattern ( c, h, r, i );

				}

			}

		}

		return image;

	}

	static bool matches ( machine::bus& memory, uint32_t address, uint32_t cylinder, uint32_t head, uint32_t record ) {

		for ( uint32_t i = 0; i < machine::floppy::sector_size; i++ ) {

			if ( memory.read8 ( address + i ) != pattern ( cylinder, head, record, i ) )
				return false;

		}

		return true;

	}

	static void format_tests ( ) {

		const machine::floppy_format* small = machine::floppy::identify ( 368640 );
		const machine::floppy_format* large = machine::floppy::identify ( 1474560 );

		expect ( small && small->cylinders == 40 && small->heads == 2 && small->sectors == 9, "floppy: a 360 KB image is 40 cylinders, 2 heads, 9 sectors" );
		expect ( large && large->cylinders == 80 && large->sectors == 18, "floppy: a 1.44 MB image is 80 cylinders, 18 sectors" );
		expect ( !machine::floppy::identify ( 368641 ) && !machine::floppy::identify ( 65536 ), "floppy: other sizes are not floppy images" );

	}

	static void controller_tests ( ) {

		floppy_rig* rig = new floppy_rig ( );
		machine::fdc& chip = rig->controller;
		base::list < uint8_t > image = make_image ( *machine::floppy::identify ( 368640 ) );
		uint8_t status = 0;
		uint8_t cylinder = 0;

		expect ( chip.insert ( 0, image.data ( ), static_cast < uint32_t > ( image.count ( ) ), false ), "765: a 360 KB image goes into drive A" );
		expect ( chip.in ( 0x3F4 ) == 0x00, "765: the controller holds still while the adapter keeps it in reset" );

		chip.out ( 0x3F2, 0x1C );
		expect ( chip.interrupt ( ) && chip.in ( 0x3F4 ) == 0x80, "765: leaving reset raises IRQ6 and the controller waits for a command" );
		expect ( rig->sense ( status, cylinder ) && status == 0xC0 && !chip.interrupt ( ), "765: the first sense after reset reports drive 0's ready change and drops IRQ6" );
		expect ( rig->sense ( status, cylinder ) && status == 0xC1, "765: further senses report the other drives" );

		static const uint8_t specify [ ] = { 0x03, 0xDF, 0x02 };
		static const uint8_t lonely [ ] = { 0x08 };

		rig->send ( specify, 3 );
		expect ( rig->send ( lonely, 1 ) && rig->receive ( ) == 1 && rig->results [ 0 ] == 0x80, "765: once another command runs, a sense with nothing to report is invalid" );

		chip.out ( 0x3F2, 0x14 );
		chip.out ( 0x3F2, 0x00 );
		chip.out ( 0x3F2, 0x14 );
		expect ( !chip.interrupt ( ), "765: IRQ6 stays off while the adapter's interrupt enable is clear" );
		chip.out ( 0x3F2, 0x1C );
		rig->sense ( status, cylinder );
		rig->send ( specify, 3 );

		static const uint8_t seek [ ] = { 0x0F, 0x00, 0x05 };

		rig->send ( seek, 3 );
		expect ( ( chip.in ( 0x3F4 ) & 0x81 ) == 0x81, "765: a seek runs in the background and marks drive 0 busy" );
		expect ( rig->settle ( 400000 ) && rig->sense ( status, cylinder ) && status == 0x20 && cylinder == 5, "765: the seek ends on cylinder 5 with seek end set" );

		static const uint8_t read [ ] = { 0xE6, 0x00, 0x05, 0x00, 0x01, 0x02, 0x09, 0x2A, 0xFF };

		rig->channel ( 0x46, 0x10000, 1024 );
		rig->send ( read, 9 );
		expect ( chip.in ( 0x3F4 ) == 0x10, "765: during a DMA transfer the controller is busy and not ready for bytes" );

		bool done = rig->settle ( 400000 );
		int32_t count = rig->receive ( );
		uint8_t* r = rig->results;

		expect ( done && count == 7 && r [ 0 ] == 0x00 && r [ 1 ] == 0x00 && r [ 3 ] == 5 && r [ 4 ] == 0 && r [ 5 ] == 3 && r [ 6 ] == 2, "765: a two sector read stops at terminal count and points at the next sector" );
		expect ( matches ( rig->memory, 0x10000, 5, 0, 1 ) && matches ( rig->memory, 0x10200, 5, 0, 2 ), "765: the sectors land in memory through DMA channel 2" );
		expect ( !chip.interrupt ( ), "765: reading the first result byte drops IRQ6" );

		static const uint8_t across [ ] = { 0xE6, 0x00, 0x05, 0x00, 0x09, 0x02, 0x09, 0x2A, 0xFF };

		rig->channel ( 0x46, 0x20000, 1024 );
		rig->send ( across, 9 );
		rig->settle ( 400000 );
		rig->receive ( );
		expect ( r [ 0 ] == 0x04 && r [ 3 ] == 5 && r [ 4 ] == 1 && r [ 5 ] == 2 && matches ( rig->memory, 0x20000, 5, 0, 9 ) && matches ( rig->memory, 0x20200, 5, 1, 1 ), "765: a multi-track read carries on from the last sector of head 0 to head 1" );

		static const uint8_t past [ ] = { 0x66, 0x00, 0x05, 0x00, 0x08, 0x02, 0x09, 0x2A, 0xFF };

		rig->channel ( 0x46, 0x30000, 4096 );
		rig->send ( past, 9 );
		rig->settle ( 600000 );
		rig->receive ( );
		expect ( r [ 0 ] == 0x40 && r [ 1 ] == 0x80 && r [ 3 ] == 6 && r [ 5 ] == 1 && matches ( rig->memory, 0x30200, 5, 0, 9 ), "765: without terminal count a read ends at the end of the cylinder" );

		static const uint8_t wrong [ ] = { 0x66, 0x00, 0x06, 0x00, 0x01, 0x02, 0x09, 0x2A, 0xFF };

		rig->channel ( 0x46, 0x30000, 512 );
		rig->send ( wrong, 9 );
		rig->settle ( 400000 );
		rig->receive ( );
		expect ( r [ 0 ] == 0x40 && r [ 1 ] == 0x04 && r [ 2 ] == 0x10, "765: asking for a cylinder the head isn't on reports no data and wrong cylinder" );

		for ( uint32_t i = 0; i < 512; i++ )
			rig->memory.write8 ( 0x40000 + i, static_cast < uint8_t > ( i ^ 0xA5 ) );

		static const uint8_t write [ ] = { 0x45, 0x00, 0x05, 0x00, 0x04, 0x02, 0x09, 0x2A, 0xFF };

		rig->channel ( 0x4A, 0x40000, 512 );
		rig->send ( write, 9 );
		rig->settle ( 400000 );
		rig->receive ( );

		uint8_t* written = chip.drives [ 0 ].disk.sector ( 5, 0, 4 );
		bool same = written != nullptr;

		for ( uint32_t i = 0; same && i < 512; i++ )
			same = written [ i ] == static_cast < uint8_t > ( i ^ 0xA5 );

		expect ( r [ 0 ] == 0x00 && same && chip.drives [ 0 ].disk.changed, "765: a write takes the sector from memory and marks the disk changed" );

		static const uint8_t identify [ ] = { 0x4A, 0x04 };

		rig->send ( identify, 2 );
		rig->settle ( 100000 );
		rig->receive ( );
		expect ( r [ 0 ] == 0x04 && r [ 3 ] == 5 && r [ 4 ] == 1 && r [ 6 ] == 2, "765: read ID reports the cylinder under the head" );

		uint8_t fields [ 36 ];

		for ( uint8_t i = 0; i < 9; i++ ) {

			fields [ i * 4 ] = 5;
			fields [ i * 4 + 1 ] = 1;
			fields [ i * 4 + 2 ] = static_cast < uint8_t > ( i + 1 );
			fields [ i * 4 + 3 ] = 2;

		}

		for ( uint32_t i = 0; i < 36; i++ )
			rig->memory.write8 ( 0x50000 + i, fields [ i ] );

		static const uint8_t format [ ] = { 0x4D, 0x04, 0x02, 0x09, 0x50, 0xF6 };

		rig->channel ( 0x4A, 0x50000, 36 );
		rig->send ( format, 6 );
		rig->settle ( 1500000 );
		rig->receive ( );

		uint8_t* blank = chip.drives [ 0 ].disk.sector ( 5, 1, 7 );

		expect ( r [ 0 ] == 0x04 && blank && blank [ 0 ] == 0xF6 && blank [ 511 ] == 0xF6, "765: formatting a track fills every sector with the filler byte" );

		static const uint8_t drive_state [ ] = { 0x04, 0x00 };

		chip.drives [ 0 ].disk.protect = true;
		rig->send ( drive_state, 2 );
		rig->receive ( );
		expect ( ( r [ 0 ] & 0x78 ) == 0x68, "765: sense drive status shows write protect, ready and two sides" );

		rig->channel ( 0x4A, 0x40000, 512 );
		rig->send ( write, 9 );
		rig->settle ( 400000 );
		rig->receive ( );
		expect ( r [ 0 ] == 0x40 && r [ 1 ] == 0x02, "765: writing to a protected disk fails with not writable" );

		static const uint8_t empty [ ] = { 0x66, 0x01, 0x00, 0x00, 0x01, 0x02, 0x09, 0x2A, 0xFF };

		rig->channel ( 0x46, 0x30000, 512 );
		rig->send ( empty, 9 );
		expect ( !rig->settle ( 2000000 ) && chip.in ( 0x3F4 ) == 0x10, "765: reading an empty drive never finishes, so the BIOS times out" );

		chip.out ( 0x3F2, 0x18 );
		chip.out ( 0x3F2, 0x1C );
		expect ( chip.interrupt ( ) && chip.in ( 0x3F4 ) == 0x80, "765: an adapter reset frees the controller again" );

		static const uint8_t unknown [ ] = { 0x10 };

		rig->sense ( status, cylinder );
		expect ( rig->send ( unknown, 1 ) && rig->receive ( ) == 1 && r [ 0 ] == 0x80, "765: unknown commands get a single invalid status byte" );

		delete rig;

	}

	static void boot_tests ( ) {

		machine::xt* board = new machine::xt ( );
		base::list < uint8_t > image;

		image.fill ( 368640, 0 );

		static const uint8_t sector [ ] = {

			0xB8, 0x00, 0xB8, 0x8E, 0xC0,
			0x26, 0xC7, 0x06, 0x00, 0x00, 0x42, 0x1F,
			0x26, 0xC7, 0x06, 0x02, 0x00, 0x4F, 0x1F,
			0x26, 0xC7, 0x06, 0x04, 0x00, 0x4F, 0x1F,
			0x26, 0xC7, 0x06, 0x06, 0x00, 0x54, 0x1F,
			0xFA, 0xF4, 0xEB, 0xFD,

		};

		for ( uint32_t i = 0; i < sizeof ( sector ); i++ )
			image [ i ] = sector [ i ];

		image [ 510 ] = 0x55;
		image [ 511 ] = 0xAA;

		program code;

		code.size = 0x1000;

		uint32_t send = code.here ( );

		code.emit ( 0x88 ).emit ( 0xC4 ).mov_dx ( 0x3F4 );

		uint32_t send_wait = code.here ( );

		code.emit ( 0xEC ).emit ( 0x24 ).emit ( 0xC0 ).emit ( 0x3C ).emit ( 0x80 ).branch ( 0x75, send_wait );
		code.emit ( 0x42 ).emit ( 0x88 ).emit ( 0xE0 ).emit ( 0xEE ).emit ( 0xC3 );

		uint32_t receive = code.here ( );

		code.mov_dx ( 0x3F4 );

		uint32_t receive_wait = code.here ( );

		code.emit ( 0xEC ).emit ( 0x24 ).emit ( 0xC0 ).emit ( 0x3C ).emit ( 0xC0 ).branch ( 0x75, receive_wait );
		code.emit ( 0x42 ).emit ( 0xEC ).emit ( 0xC3 );

		uint32_t wait = code.here ( );

		code.emit ( 0xFB );

		uint32_t wait_loop = code.here ( );

		code.emit ( 0x80 ).emit ( 0x3E ).word ( 0x0500 ).emit ( 0x00 ).branch ( 0x74, wait_loop );
		code.store_byte ( 0x0500, 0 ).emit ( 0xC3 );

		uint32_t handler = code.here ( );

		code.store_byte ( 0x0500, 1 ).emit ( 0x50 ).out_byte ( 0x20, 0x20 ).emit ( 0x58 ).emit ( 0xCF );

		code.size = 0;
		code.emit ( 0xFA ).mov_ax ( 0x0030 ).emit ( 0x8E ).emit ( 0xD0 ).emit ( 0xBC ).word ( 0x0100 );
		code.emit ( 0x31 ).emit ( 0xC0 ).emit ( 0x8E ).emit ( 0xD8 ).emit ( 0x8E ).emit ( 0xC0 );
		code.store ( 0x0038, static_cast < uint16_t > ( 0xE000 + handler ) ).store ( 0x003A, 0xF000 ).store ( 0x0500, 0 ).store ( 0x0502, 0xFFFF );
		code.out_byte ( 0x20, 0x13 ).out_byte ( 0x21, 0x08 ).out_byte ( 0x21, 0x09 ).out_byte ( 0x21, 0xBF );
		code.mov_dx ( 0x3F2 ).mov_al ( 0x00 ).emit ( 0xEE ).mov_al ( 0x1C ).emit ( 0xEE );
		code.call ( wait ).mov_al ( 0x08 ).call ( send ).call ( receive ).call ( receive );
		code.mov_al ( 0x07 ).call ( send ).mov_al ( 0x00 ).call ( send ).call ( wait );
		code.mov_al ( 0x08 ).call ( send ).call ( receive ).call ( receive );
		code.out_byte ( 0x0A, 0x06 ).out_byte ( 0x0C, 0x00 ).out_byte ( 0x0B, 0x46 ).out_byte ( 0x04, 0x00 ).out_byte ( 0x04, 0x7C );
		code.out_byte ( 0x81, 0x00 ).out_byte ( 0x05, 0xFF ).out_byte ( 0x05, 0x01 ).out_byte ( 0x0A, 0x02 );

		static const uint8_t read [ ] = { 0xE6, 0x00, 0x00, 0x00, 0x01, 0x02, 0x09, 0x2A, 0xFF };

		for ( uint8_t value : read )
			code.mov_al ( value ).call ( send );

		code.call ( wait ).call ( receive ).emit ( 0xA2 ).word ( 0x0502 );

		for ( int32_t i = 0; i < 6; i++ )
			code.call ( receive );

		code.emit ( 0xEA ).word ( 0x7C00 ).word ( 0x0000 );
		code.size = 0x1FF0;
		code.emit ( 0xEA ).word ( 0xE000 ).word ( 0xF000 );
		code.size = 0x2000;

		board->load_bios ( code.bytes, code.size );
		board->floppy.insert ( 0, image.data ( ), static_cast < uint32_t > ( image.count ( ) ), false );
		board->power_on ( );
		board->run ( machine::xt::clock_rate / 5 );

		bool booted = board->memory.read8 ( 0xB8000 ) == 'B' && board->memory.read8 ( 0xB8002 ) == 'O' && board->memory.read8 ( 0xB8006 ) == 'T';

		expect ( board->memory.read8 ( 0x0502 ) == 0x00 && board->memory.read16 ( 0x7DFE ) == 0xAA55, "XT: a ROM reads the boot sector to 0000:7C00 through the 765, DMA and IRQ6" );
		expect ( booted, "XT: the CPU jumps into the boot sector and it runs" );

		delete board;

	}

	static void image_tests ( ) {

		base::list < uint8_t > disk;
		bool made = machine::make_blank_disk ( 1474560, 0x1234ABCD, disk );

		expect ( made && disk.count ( ) == 1474560 && disk [ 0 ] == 0xEB && disk [ 0x0C ] == 0x02 && disk [ 0x11 ] == 0xE0 && disk [ 0x15 ] == 0xF0 && disk [ 0x18 ] == 18 && disk [ 510 ] == 0x55 && disk [ 511 ] == 0xAA && disk [ 512 ] == 0xF0, "disks: a blank 1.44 MB image gets a boot sector, a BPB and empty FATs" );

		static const char* const source = "org 0x7C00\r\nmov ax, 0x1234\r\njmp $\r\n";
		char error [ 160 ] = { };
		uint32_t used = 0;

		made = machine::make_program_disk ( source, base::text_length ( source ), disk, used, error, sizeof ( error ) );

		bool named = machine::is_program_path ( L"C:\\demos\\Mandelbrot.ASM" ) && !machine::is_program_path ( L"C:\\disks\\asm.img" );

		expect ( made && named && used == 5 && disk.count ( ) == static_cast < int32_t > ( machine::program_disk_size ) && disk [ 0 ] == 0xB8 && disk [ 2 ] == 0x12 && disk [ 3 ] == 0xEB && disk [ 4 ] == 0xFE && disk [ 510 ] == 0x55 && disk [ 511 ] == 0xAA, "disks: an .asm file with CRLF lines becomes a bootable 360 KB image" );

		static const char* const broken = "org 0x7C00\nmov ax, 1\nfrobnicate ax\n";

		made = machine::make_program_disk ( broken, base::text_length ( broken ), disk, used, error, sizeof ( error ) );

		expect ( !made && error [ 0 ] == 'l' && error [ 5 ] == '3' && error [ 6 ] == ':', "disks: a broken program reports the line it broke on" );

	}

	void storage_tests ( ) {

		print ( "\r\n" );
		format_tests ( );
		controller_tests ( );
		boot_tests ( );
		image_tests ( );

	}

}

#endif