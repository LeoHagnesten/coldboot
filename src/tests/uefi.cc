#ifdef COLDBOOT_DEBUG

#include "base/files.hh"
#include "pc/computer.hh"
#include "pc/fat.hh"
#include "pc/pe.hh"
#include "tests/harness.hh"
#include <windows.h>

namespace tests {

	static void put16 ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value );
		at [ 1 ] = static_cast < uint8_t > ( value >> 8 );

	}

	static void put32 ( uint8_t* at, uint32_t value ) {

		put16 ( at, value );
		put16 ( at + 2, value >> 16 );

	}

	static void put64 ( uint8_t* at, uint64_t value ) {

		put32 ( at, static_cast < uint32_t > ( value ) );
		put32 ( at + 4, static_cast < uint32_t > ( value >> 32 ) );

	}

	static void build_program ( uint8_t* file ) {

		for ( uint32_t i = 0; i < 0x600; i++ )
			file [ i ] = 0;

		static const uint8_t code [ ] = {

			0x48, 0x83, 0xEC, 0x28,
			0x48, 0x8B, 0x4A, 0x40,
			0x48, 0x8B, 0x15, 0xF1, 0x00, 0x00, 0x00,
			0xFF, 0x51, 0x08,
			0x31, 0xC0,
			0x48, 0x83, 0xC4, 0x28,
			0xC3,

		};

		file [ 0 ] = 'M';
		file [ 1 ] = 'Z';
		put32 ( file + 0x3C, 0x40 );
		put32 ( file + 0x40, 0x00004550 );
		put16 ( file + 0x44, 0x8664 );
		put16 ( file + 0x46, 1 );
		put16 ( file + 0x54, 240 );
		put16 ( file + 0x56, 0x22 );

		uint8_t* optional = file + 0x58;

		put16 ( optional, 0x20B );
		put32 ( optional + 16, 0x1000 );
		put32 ( optional + 20, 0x1000 );
		put64 ( optional + 24, 0x10000000 );
		put32 ( optional + 32, 0x1000 );
		put32 ( optional + 36, 0x200 );
		put32 ( optional + 56, 0x2000 );
		put32 ( optional + 60, 0x200 );
		put16 ( optional + 68, 10 );
		put32 ( optional + 108, 16 );
		put32 ( optional + 112 + 5 * 8, 0x1200 );
		put32 ( optional + 112 + 5 * 8 + 4, 12 );

		uint8_t* section = file + 0x58 + 240;
		static const char name [ ] = ".text";

		for ( int32_t i = 0; name [ i ]; i++ )
			section [ i ] = static_cast < uint8_t > ( name [ i ] );

		put32 ( section + 8, 0x1000 );
		put32 ( section + 12, 0x1000 );
		put32 ( section + 16, 0x400 );
		put32 ( section + 20, 0x200 );
		put32 ( section + 36, 0xE0000020 );

		uint8_t* raw = file + 0x200;

		for ( uint32_t i = 0; i < sizeof ( code ); i++ )
			raw [ i ] = code [ i ];

		static const char text [ ] = "OK\r\n";

		for ( int32_t i = 0; text [ i ]; i++ )
			put16 ( raw + 0x80 + i * 2, static_cast < uint8_t > ( text [ i ] ) );

		put64 ( raw + 0x100, 0x10001080 );
		put32 ( raw + 0x200, 0x1000 );
		put32 ( raw + 0x204, 12 );
		put16 ( raw + 0x208, 10u << 12 | 0x100 );

	}

	static void short_entry ( uint8_t* at, const char* name, uint8_t attributes, uint32_t cluster, uint32_t size ) {

		for ( int32_t i = 0; i < 11; i++ )
			at [ i ] = static_cast < uint8_t > ( name [ i ] );

		at [ 11 ] = attributes;
		put16 ( at + 20, cluster >> 16 );
		put16 ( at + 26, cluster );
		put32 ( at + 28, size );

	}

	static void build_volume ( uint8_t* volume, const uint8_t* program, uint32_t program_size ) {

		for ( uint32_t i = 0; i < 2048 * 512; i++ )
			volume [ i ] = 0;

		volume [ 0 ] = 0xEB;
		volume [ 1 ] = 0x3C;
		volume [ 2 ] = 0x90;
		put16 ( volume + 11, 512 );
		volume [ 13 ] = 1;
		put16 ( volume + 14, 1 );
		volume [ 16 ] = 1;
		put16 ( volume + 17, 32 );
		put16 ( volume + 19, 2048 );
		volume [ 21 ] = 0xF8;
		put16 ( volume + 22, 6 );
		volume [ 510 ] = 0x55;
		volume [ 511 ] = 0xAA;

		uint8_t* fat = volume + 512;
		uint32_t program_clusters = ( program_size + 511 ) / 512;
		auto link = [ & ] ( uint32_t cluster, uint32_t next ) {

			uint32_t at = cluster + cluster / 2;
			uint32_t value = fat [ at ] | fat [ at + 1 ] << 8;

			value = cluster & 1 ? ( value & 0x000F ) | next << 4 : ( value & 0xF000 ) | next;
			put16 ( fat + at, value );

		};

		link ( 0, 0xFF8 );
		link ( 1, 0xFFF );
		link ( 2, 0xFFF );
		link ( 3, 0xFFF );

		for ( uint32_t i = 0; i < program_clusters; i++ )
			link ( 4 + i, i + 1 < program_clusters ? 5 + i : 0xFFF );

		uint32_t note_cluster = 4 + program_clusters;

		link ( note_cluster, 0xFFF );

		uint8_t* root = volume + 7 * 512;
		static const char long_name [ ] = "Coldboot notes.txt";

		root [ 0 ] = 0x42;
		root [ 32 ] = 0x01;

		for ( int32_t part = 0; part < 2; part++ ) {

			uint8_t* entry = root + ( part == 0 ? 0 : 32 );
			static const uint8_t places [ 13 ] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
			int32_t first = part == 0 ? 13 : 0;

			entry [ 11 ] = 0x0F;

			for ( int32_t c = 0; c < 13; c++ ) {

				int32_t index = first + c;
				uint16_t value = index < 18 ? static_cast < uint16_t > ( long_name [ index ] ) : ( index == 18 ? 0 : 0xFFFF );

				put16 ( entry + places [ c ], value );

			}

		}

		short_entry ( root + 64, "COLDBO~1TXT", 0x20, note_cluster, 5 );
		short_entry ( root + 96, "EFI        ", 0x10, 2, 0 );

		uint8_t* data = volume + 9 * 512;

		short_entry ( data, ".          ", 0x10, 2, 0 );
		short_entry ( data + 32, "..         ", 0x10, 0, 0 );
		short_entry ( data + 64, "BOOT       ", 0x10, 3, 0 );
		short_entry ( data + 512, ".          ", 0x10, 3, 0 );
		short_entry ( data + 544, "..         ", 0x10, 2, 0 );
		short_entry ( data + 576, "BOOTX64 EFI", 0x20, 4, program_size );

		for ( uint32_t i = 0; i < program_size; i++ )
			data [ 2 * 512 + i ] = program [ i ];

		static const char note [ ] = "hello";

		for ( int32_t i = 0; note [ i ]; i++ )
			data [ ( note_cluster - 2 ) * 512 + i ] = static_cast < uint8_t > ( note [ i ] );

	}

	static void build_disk ( uint8_t* disk ) {

		for ( uint32_t i = 0; i < 2048 * 512; i++ )
			disk [ i ] = 0;

		uint8_t* entry = disk + 446;

		entry [ 4 ] = 0xEE;
		put32 ( entry + 8, 1 );
		put32 ( entry + 12, 8191 );
		disk [ 510 ] = 0x55;
		disk [ 511 ] = 0xAA;

		uint8_t* header = disk + 512;
		static const char signature [ ] = "EFI PART";

		for ( int32_t i = 0; i < 8; i++ )
			header [ i ] = static_cast < uint8_t > ( signature [ i ] );

		put32 ( header + 8, 0x00010000 );
		put32 ( header + 12, 92 );
		put64 ( header + 24, 1 );
		put64 ( header + 32, 8191 );
		put64 ( header + 40, 34 );
		put64 ( header + 48, 8158 );
		put64 ( header + 72, 2 );
		put32 ( header + 80, 4 );
		put32 ( header + 84, 128 );

		static const uint8_t system [ 16 ] = { 0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B };
		uint8_t* partition = disk + 1024;

		for ( int32_t i = 0; i < 16; i++ ) {

			partition [ i ] = system [ i ];
			partition [ 16 + i ] = static_cast < uint8_t > ( 0xC0 + i );

		}

		put64 ( partition + 32, 2048 );
		put64 ( partition + 40, 4095 );

	}

	void uefi_tests ( ) {

		uint8_t* program = static_cast < uint8_t* > ( runtime_allocate_zero ( 0x600 ) );
		uint8_t* placed = static_cast < uint8_t* > ( runtime_allocate_zero ( 0x2000 ) );
		pc::pe_info info;

		build_program ( program );

		bool inspected = pc::pe_inspect ( program, 0x600, info ) && info.machine == 0x8664 && info.image_size == 0x2000 && info.entry == 0x1000 && info.subsystem == 10;
		bool moved = pc::pe_place ( program, 0x600, placed, 0x20000000 ) && placed [ 0x1000 ] == 0x48 && placed [ 0x1100 ] == 0x80 && placed [ 0x1101 ] == 0x10 && placed [ 0x1103 ] == 0x20;

		expect ( inspected && moved, "uefi: the PE loader reads an x64 EFI program, lays out its section and applies a DIR64 relocation" );

		uint8_t* volume = static_cast < uint8_t* > ( runtime_allocate_zero ( 2048 * 512 ) );
		uint8_t* disk = static_cast < uint8_t* > ( runtime_allocate_zero ( 4096 * 512 ) );
		wchar_t path [ MAX_PATH ];

		build_volume ( volume, program, 0x600 );
		build_disk ( disk );

		for ( uint32_t i = 0; i < 2048 * 512; i++ )
			disk [ 2048 * 512 + i ] = volume [ i ];

		GetTempPathW ( MAX_PATH, path );

		base::writer target;

		target.add ( path ).add ( L"coldboot-uefi-test.img" );

		bool written = files::write ( target.text ( ), reinterpret_cast < const char* > ( disk ), 4096 * 512 );
		pc::disk_file* media = new pc::disk_file ( );
		pc::fat_reader* reader = new pc::fat_reader ( );
		base::list < uint8_t > found;
		base::list < uint8_t > note;
		bool opened = written && media->open ( target.text ( ), false ) && reader->open ( media, 2048 * 512, 2048 * 512 );
		bool loaded = opened && reader->read_file ( "\\EFI\\BOOT\\BOOTX64.EFI", found ) && found.size ( ) == 0x600 && found [ 0x200 ] == 0x48;
		bool long_named = opened && reader->read_file ( "\\coldboot NOTES.txt", note ) && note.size ( ) == 5 && note [ 0 ] == 'h';

		delete reader;
		delete media;
		expect ( loaded && long_named, "uefi: the FAT reader walks folders and finds files by short and long names" );

		if ( hv::present ( ) && written ) {

			pc::computer_options* options = new pc::computer_options ( );
			pc::computer* machine = new pc::computer ( );
			char console [ 1024 ] = { };

			options->memory = 128ull * 1024 * 1024;
			options->uefi = true;
			options->network = false;

			for ( int32_t i = 0; target.text ( ) [ i ]; i++ )
				options->disk [ i ] = target.text ( ) [ i ];

			bool booted = machine->power_on ( *options );
			auto contains = [ & ] ( const char* text ) {

				for ( int32_t i = 0; console [ i ]; i++ ) {

					int32_t j = 0;

					while ( text [ j ] && console [ i + j ] == text [ j ] )
						j++;

					if ( !text [ j ] )
						return true;

				}

				return false;

			};

			for ( int32_t waited = 0; booted && waited < 400 && !contains ( "No bootable device" ); waited++ ) {

				Sleep ( 10 );
				machine->console_text ( console, sizeof ( console ) );

			}

			bool returned = contains ( "boot image returned 0000000000000000" );
			bool printed = contains ( "OK\r\n" );
			bool from_disk = contains ( "UEFI: booting the hard disk" );

			machine->power_off ( );
			delete machine;
			delete options;
			expect ( booted && printed && returned && from_disk, "uefi: the firmware boots a GPT disk's system partition, runs its EFI program, prints through ConOut and survives the return" );

		}

		DeleteFileW ( target.text ( ) );
		runtime_release ( program );
		runtime_release ( placed );
		runtime_release ( volume );
		runtime_release ( disk );

	}

}

#endif