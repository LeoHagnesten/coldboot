#include "storage/blank.hh"
#include "assembler/assembler.hh"
#include "storage/floppy.hh"

namespace machine {

	struct disk_layout {

		uint32_t size;
		uint8_t cluster;
		uint16_t root;
		uint8_t media;
		uint8_t fat;

	};

	static const disk_layout layouts [ ] = {

		{ 163840, 1, 64, 0xFE, 1 },
		{ 184320, 1, 64, 0xFC, 2 },
		{ 327680, 2, 112, 0xFF, 1 },
		{ 368640, 2, 112, 0xFD, 2 },
		{ 737280, 2, 112, 0xF9, 3 },
		{ 1228800, 1, 224, 0xF9, 7 },
		{ 1474560, 1, 224, 0xF0, 9 },

	};

	static const char* const boot_source =
		"org 0x7C00\n"
		"jmp short start\n"
		"nop\n"
		"db \"COLDBOOT\"\n"
		"times 0x3E - ( $ - $$ ) db 0\n"
		"start: cli\n"
		"xor ax, ax\n"
		"mov ds, ax\n"
		"mov ss, ax\n"
		"mov sp, 0x7C00\n"
		"sti\n"
		"mov si, message\n"
		"next: lodsb\n"
		"test al, al\n"
		"jz idle\n"
		"mov ah, 0x0E\n"
		"mov bx, 0x0007\n"
		"int 0x10\n"
		"jmp next\n"
		"idle: xor ah, ah\n"
		"int 0x16\n"
		"int 0x19\n"
		"message: db 13, 10, \"This disk has no system on it. Insert a boot disk and press any key.\", 13, 10, 0\n"
		"times 510 - ( $ - $$ ) db 0\n"
		"dw 0xAA55\n";

	static void put16 ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value );
		at [ 1 ] = static_cast < uint8_t > ( value >> 8 );

	}

	bool make_blank_disk ( uint32_t size, uint32_t serial, base::list < uint8_t >& out ) {

		const floppy_format* format = floppy::identify ( size );
		const disk_layout* layout = nullptr;

		for ( const disk_layout& entry : layouts ) {

			if ( entry.size == size )
				layout = &entry;

		}

		if ( !format || !layout )
			return false;

		assembler::result* boot = new assembler::result ( );

		if ( !assembler::assemble ( boot_source, base::text_length ( boot_source ), nullptr, 0, *boot ) || boot->bytes.count ( ) != 512 ) {

			delete boot;

			return false;

		}

		out.fill ( size, 0 );

		uint8_t* disk = out.data ( );

		for ( int32_t i = 0; i < 512; i++ )
			disk [ i ] = boot->bytes [ i ];

		delete boot;

		put16 ( disk + 0x0B, 512 );
		disk [ 0x0D ] = layout->cluster;
		put16 ( disk + 0x0E, 1 );
		disk [ 0x10 ] = 2;
		put16 ( disk + 0x11, layout->root );
		put16 ( disk + 0x13, size / 512 );
		disk [ 0x15 ] = layout->media;
		put16 ( disk + 0x16, layout->fat );
		put16 ( disk + 0x18, format->sectors );
		put16 ( disk + 0x1A, format->heads );
		disk [ 0x26 ] = 0x29;
		put16 ( disk + 0x27, serial );
		put16 ( disk + 0x29, serial >> 16 );

		const char* label = "NO NAME    FAT12   ";

		for ( int32_t i = 0; i < 19; i++ )
			disk [ 0x2B + i ] = static_cast < uint8_t > ( label [ i ] );

		for ( uint32_t copy = 0; copy < 2; copy++ ) {

			uint8_t* fat = disk + 512 + copy * layout->fat * 512u;

			fat [ 0 ] = layout->media;
			fat [ 1 ] = 0xFF;
			fat [ 2 ] = 0xFF;

		}

		uint32_t data_start = ( 1u + 2u * layout->fat ) * 512u + layout->root * 32u;

		for ( uint32_t i = data_start; i < size; i++ )
			disk [ i ] = 0xF6;

		return true;

	}

}