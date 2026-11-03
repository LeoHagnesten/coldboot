#ifdef COLDBOOT_DEBUG

#include "assembler/assembler.hh"
#include "base/files.hh"
#include "pc/acpi.hh"
#include "pc/aml.hh"
#include "pc/computer.hh"
#include "pc/iso.hh"
#include "pc/smbios.hh"
#include "tests/harness.hh"
#include <windows.h>

namespace tests {

	struct quiet_bus : pc::platform_bus {

		bool lines [ 16 ] = { };
		int32_t raised = 0;

		void set_irq ( uint32_t irq, bool level ) override {

			if ( irq < 16 ) {

				if ( level && !lines [ irq ] )
					raised++;

				lines [ irq ] = level;

			}

		}

		void request_reset ( ) override { }

		void request_power_off ( ) override { }

		void console ( char ) override { }

		uint64_t now ( ) override {

			return 0;

		}

	};

	class aml_walker {

	public:

		aml_walker ( const uint8_t* bytes, uint32_t size ) : data ( bytes ), total ( size ) { }

		bool run ( ) {

			uint32_t at = 0;

			return terms ( at, total ) && at == total;

		}

		int32_t devices = 0;
		int32_t processors = 0;
		int32_t methods = 0;
		int32_t names = 0;

	private:

		bool length ( uint32_t& at, uint32_t& end ) {

			if ( at >= total )
				return false;

			uint32_t start = at;
			uint8_t lead = data [ at++ ];
			uint32_t extra = lead >> 6;
			uint32_t value = extra ? lead & 0x0F : lead & 0x3F;

			for ( uint32_t i = 0; i < extra; i++ ) {

				if ( at >= total )
					return false;

				value |= static_cast < uint32_t > ( data [ at++ ] ) << ( 4 + i * 8 );

			}

			end = start + value;

			return end <= total && end >= at;

		}

		bool segment ( uint32_t& at ) {

			if ( at + 4 > total )
				return false;

			for ( int32_t i = 0; i < 4; i++ ) {

				uint8_t c = data [ at + i ];
				bool ok = ( c >= 'A' && c <= 'Z' ) || c == '_' || ( i > 0 && c >= '0' && c <= '9' );

				if ( !ok )
					return false;

			}

			at += 4;

			return true;

		}

		bool path ( uint32_t& at ) {

			while ( at < total && ( data [ at ] == '\\' || data [ at ] == '^' ) )
				at++;

			if ( at >= total )
				return false;

			if ( data [ at ] == 0x00 ) {

				at++;

				return true;

			}

			uint32_t count = 1;

			if ( data [ at ] == 0x2E ) {

				count = 2;
				at++;

			} else if ( data [ at ] == 0x2F ) {

				if ( at + 1 >= total )
					return false;

				count = data [ at + 1 ];
				at += 2;

			}

			for ( uint32_t i = 0; i < count; i++ ) {

				if ( !segment ( at ) )
					return false;

			}

			return true;

		}

		bool object ( uint32_t& at, uint32_t limit ) {

			if ( at >= limit )
				return false;

			uint8_t code = data [ at++ ];

			switch ( code ) {

				case 0x00:
				case 0x01:
				case 0xFF:
					return true;

				case 0x0A:
					at += 1;
					return at <= limit;

				case 0x0B:
					at += 2;
					return at <= limit;

				case 0x0C:
					at += 4;
					return at <= limit;

				case 0x0E:
					at += 8;
					return at <= limit;

				case 0x0D:

					while ( at < limit && data [ at ] )
						at++;

					at++;

					return at <= limit;

				case 0x11: {

					uint32_t end = 0;

					if ( !length ( at, end ) || end > limit || !object ( at, end ) )
						return false;

					return resources ( at, end ) && ( at = end, true );

				}

				case 0x12: {

					uint32_t end = 0;

					if ( !length ( at, end ) || end > limit || at >= end )
						return false;

					uint32_t count = data [ at++ ];

					for ( uint32_t i = 0; i < count; i++ ) {

						if ( !object ( at, end ) )
							return false;

					}

					return at == end;

				}

				default:
					return false;

			}

		}

		bool resources ( uint32_t at, uint32_t end ) {

			while ( at < end ) {

				uint8_t tag = data [ at ];

				if ( tag & 0x80 ) {

					if ( at + 3 > end )
						return false;

					uint32_t size = static_cast < uint32_t > ( data [ at + 1 ] ) | static_cast < uint32_t > ( data [ at + 2 ] ) << 8;

					at += 3 + size;

				} else {

					if ( tag == 0x79 )
						return at + 2 == end;

					at += 1 + ( tag & 7 );

				}

			}

			return false;

		}

		bool terms ( uint32_t& at, uint32_t end ) {

			while ( at < end ) {

				uint8_t code = data [ at++ ];

				if ( code == 0x08 ) {

					names++;

					if ( !path ( at ) || !object ( at, end ) )
						return false;

				} else if ( code == 0x10 || code == 0x14 ) {

					uint32_t inner = 0;

					if ( !length ( at, inner ) || inner > end || !path ( at ) )
						return false;

					if ( code == 0x14 ) {

						methods++;
						at++;

					}

					if ( !terms ( at, inner ) || at != inner )
						return false;

				} else if ( code == 0x5B ) {

					if ( at >= end )
						return false;

					uint8_t extended = data [ at++ ];
					uint32_t inner = 0;

					if ( ( extended != 0x82 && extended != 0x83 ) || !length ( at, inner ) || inner > end || !path ( at ) )
						return false;

					if ( extended == 0x83 ) {

						processors++;
						at += 6;

						if ( at != inner )
							return false;

					} else {

						devices++;

						if ( !terms ( at, inner ) || at != inner )
							return false;

					}

				} else if ( code == 0xA4 ) {

					if ( !object ( at, end ) )
						return false;

				} else
					return false;

			}

			return at == end;

		}

		const uint8_t* data;
		uint32_t total;

	};

	static void temporary_path ( wchar_t* out, const wchar_t* name ) {

		wchar_t folder [ MAX_PATH ];
		base::writer path;

		GetTempPathW ( MAX_PATH, folder );
		path.add ( folder ).add ( L"coldboot-modern-" ).number ( GetCurrentProcessId ( ) ).add ( L"-" ).add ( name );

		int32_t i = 0;

		for ( ; path.text ( ) [ i ] && i < MAX_PATH - 1; i++ )
			out [ i ] = path.text ( ) [ i ];

		out [ i ] = 0;

	}

	static bool write_pattern ( const wchar_t* path, uint32_t size, uint32_t seed ) {

		base::list < char > bytes;

		bytes.resize ( size );

		for ( uint32_t i = 0; i < size; i++ )
			bytes [ i ] = static_cast < char > ( ( i * 7 + seed + i / 512 ) & 0xFF );

		return files::write ( path, bytes.data ( ), static_cast < int32_t > ( size ) );

	}

	static void aml_tests ( ) {

		pc::aml code;

		code.name ( "\\_SB.PCI0" );
		code.integer ( 0 );

		bool path_ok = code.bytes.count ( ) == 12 && code.bytes [ 11 ] == 0x00 && code.bytes [ 1 ] == '\\' && code.bytes [ 2 ] == 0x2E && code.bytes [ 3 ] == '_' && code.bytes [ 6 ] == '_' && code.bytes [ 7 ] == 'P';

		code.bytes.clear ( );
		code.eisa ( "PNP0A03" );

		bool eisa_ok = code.bytes.count ( ) == 5 && code.bytes [ 1 ] == 0x41 && code.bytes [ 2 ] == 0xD0 && code.bytes [ 3 ] == 0x0A && code.bytes [ 4 ] == 0x03;

		code.bytes.clear ( );

		int32_t tiny = code.package ( 2 );

		code.integer ( 1 );
		code.integer ( 0x1234 );
		code.close ( tiny );

		bool short_ok = code.bytes.count ( ) == 7 && code.bytes [ 1 ] == 6;

		code.bytes.clear ( );

		int32_t big = code.buffer ( );

		code.integer ( 100 );

		for ( int32_t i = 0; i < 100; i++ )
			code.byte ( 0x55 );

		code.close ( big );

		uint32_t encoded = static_cast < uint32_t > ( code.bytes [ 1 ] & 0x0F ) | static_cast < uint32_t > ( code.bytes [ 2 ] ) << 4;
		bool long_ok = ( code.bytes [ 1 ] >> 6 ) == 1 && encoded == static_cast < uint32_t > ( code.bytes.count ( ) - 1 );

		expect ( path_ok && eisa_ok && short_ok && long_ok, "modern: AML encodes name paths, EISA IDs and one- and two-byte package lengths" );

		bool every = true;
		int32_t found_devices = 0;

		static const uint32_t counts [ 4 ] = { 1, 2, 16, 64 };
		static const uint64_t memories [ 3 ] = { 512ull << 20, 3ull << 30, 16ull << 30 };

		for ( uint32_t cpus : counts ) {

			for ( uint64_t memory : memories ) {

				pc::acpi_layout layout;
				base::list < uint8_t > body;

				layout.processors = cpus;
				layout.low_memory = memory < 0xC0000000ull ? memory : 0xC0000000ull;
				layout.high_memory = memory - layout.low_memory;
				pc::dsdt_body ( layout, body );

				aml_walker walker ( body.data ( ), static_cast < uint32_t > ( body.size ( ) ) );

				every = every && walker.run ( ) && walker.processors == static_cast < int32_t > ( cpus ) && walker.methods == 1;
				found_devices = walker.devices;

			}

		}

		expect ( every && found_devices == 12, "modern: the DSDT parses back cleanly for 1 to 64 processors and 512 MB to 16 GB" );

		pc::acpi_layout layout;
		pc::acpi_image* image = new pc::acpi_image ( );

		layout.processors = 4;
		layout.low_memory = 0x80000000ull;
		layout.tables = 0x7FFC0000;
		pc::build_acpi ( layout, *image );

		const uint8_t* tables = image->tables.data ( );
		uint32_t size = static_cast < uint32_t > ( image->tables.size ( ) );
		bool sums = pc::acpi_sum ( image->rsdp, 20 ) == 0 && pc::acpi_sum ( image->rsdp, 36 ) == 0;
		int32_t found = 0;
		uint32_t fadt_length = 0;

		for ( uint32_t at = 0; at + 36 <= size; at += 16 ) {

			bool known = ( tables [ at ] == 'D' && tables [ at + 1 ] == 'S' ) || ( tables [ at ] == 'F' && tables [ at + 1 ] == 'A' && tables [ at + 2 ] == 'C' && tables [ at + 3 ] == 'P' ) || ( tables [ at ] == 'A' && tables [ at + 1 ] == 'P' && tables [ at + 2 ] == 'I' ) || ( tables [ at ] == 'R' && tables [ at + 1 ] == 'S' && tables [ at + 2 ] == 'D' ) || ( tables [ at ] == 'X' && tables [ at + 1 ] == 'S' && tables [ at + 2 ] == 'D' );

			if ( !known )
				continue;

			uint32_t length = static_cast < uint32_t > ( tables [ at + 4 ] ) | static_cast < uint32_t > ( tables [ at + 5 ] ) << 8 | static_cast < uint32_t > ( tables [ at + 6 ] ) << 16;

			if ( at + length > size )
				continue;

			sums = sums && pc::acpi_sum ( tables + at, length ) == 0;
			found++;

			if ( tables [ at ] == 'F' )
				fadt_length = length;

			at += ( ( length + 15 ) & ~15u ) - 16;

		}

		uint32_t xsdt = static_cast < uint32_t > ( image->rsdp [ 24 ] ) | static_cast < uint32_t > ( image->rsdp [ 25 ] ) << 8 | static_cast < uint32_t > ( image->rsdp [ 26 ] ) << 16 | static_cast < uint32_t > ( image->rsdp [ 27 ] ) << 24;

		expect ( sums && found == 5 && fadt_length == 244 && xsdt > layout.tables && xsdt < layout.tables + size, "modern: every ACPI table checksums to zero, the FADT is 244 bytes and the RSDP points at the XSDT" );

		delete image;

	}

	static void out8 ( pc::ide_controller& ide, uint16_t port, uint8_t value ) {

		ide.write ( port, 1, value );

	}

	static uint8_t in8 ( pc::ide_controller& ide, uint16_t port ) {

		return static_cast < uint8_t > ( ide.read ( port, 1 ) );

	}

	static void ide_tests ( ) {

		wchar_t disk_path [ MAX_PATH ];
		wchar_t disc_path [ MAX_PATH ];

		temporary_path ( disk_path, L"disk.img" );
		temporary_path ( disc_path, L"disc.iso" );

		bool made = write_pattern ( disk_path, 1024 * 1024, 0 ) && write_pattern ( disc_path, 2048 * 10, 3 );
		quiet_bus* bus = new quiet_bus ( );
		hv::machine* vm = new hv::machine ( );
		pc::ide_controller* ide = new pc::ide_controller ( *bus, *vm );
		pc::disk_file* disk = new pc::disk_file ( );
		pc::disk_file* disc = new pc::disk_file ( );

		made = made && disk->open ( disk_path, true ) && disc->open ( disc_path, false );
		ide->attach ( 0, 0, pc::DRIVE_DISK, disk );
		ide->attach ( 1, 0, pc::DRIVE_OPTICAL, disc );
		ide->reset ( );
		ide->change_media ( 1, 0, disc );

		bool signatures = in8 ( *ide, 0x1F4 ) == 0x00 && in8 ( *ide, 0x1F5 ) == 0x00 && in8 ( *ide, 0x174 ) == 0x14 && in8 ( *ide, 0x175 ) == 0xEB;

		out8 ( *ide, 0x1F6, 0xE0 );
		out8 ( *ide, 0x1F7, 0xEC );

		uint16_t words [ 256 ] = { };
		bool ready = ( in8 ( *ide, 0x1F7 ) & 0x08 ) != 0;

		for ( int32_t i = 0; i < 256; i++ )
			words [ i ] = static_cast < uint16_t > ( ide->read ( 0x1F0, 2 ) );

		uint8_t sum = 0;

		for ( int32_t i = 0; i < 256; i++ )
			sum = static_cast < uint8_t > ( sum + ( words [ i ] & 0xFF ) + ( words [ i ] >> 8 ) );

		bool identified = ready && words [ 0 ] == 0x0040 && words [ 60 ] == 2048 && words [ 61 ] == 0 && words [ 100 ] == 2048 && ( words [ 83 ] & 0x0400 ) && words [ 27 ] == ( 'C' << 8 | 'o' ) && sum == 0 && !( in8 ( *ide, 0x1F7 ) & 0x08 );

		expect ( made && signatures && identified, "modern: IDE reset leaves disk and ATAPI signatures, and IDENTIFY reports size, LBA48 and a valid checksum" );

		out8 ( *ide, 0x1F2, 2 );
		out8 ( *ide, 0x1F3, 5 );
		out8 ( *ide, 0x1F4, 0 );
		out8 ( *ide, 0x1F5, 0 );
		out8 ( *ide, 0x1F6, 0xE0 );
		out8 ( *ide, 0x1F7, 0x30 );

		for ( int32_t i = 0; i < 512; i++ )
			ide->write ( 0x1F0, 2, static_cast < uint16_t > ( 0xA000 + i ) );

		bool written = !( in8 ( *ide, 0x1F7 ) & 0x08 );
		uint8_t check [ 4 ] = { };

		disk->read ( 5 * 512, check, 2 );
		disk->read ( 6 * 512 + 2, check + 2, 2 );
		written = written && check [ 0 ] == 0x00 && check [ 1 ] == 0xA0 && check [ 2 ] == 0x01 && check [ 3 ] == 0xA1;

		out8 ( *ide, 0x1F2, 0 );
		out8 ( *ide, 0x1F3, 0 );
		out8 ( *ide, 0x1F4, 0 );
		out8 ( *ide, 0x1F5, 0 );
		out8 ( *ide, 0x1F2, 1 );
		out8 ( *ide, 0x1F3, 6 );
		out8 ( *ide, 0x1F4, 0 );
		out8 ( *ide, 0x1F5, 0 );
		out8 ( *ide, 0x1F6, 0x40 );
		out8 ( *ide, 0x1F7, 0x24 );

		uint16_t first = static_cast < uint16_t > ( ide->read ( 0x1F0, 2 ) );
		uint16_t second = static_cast < uint16_t > ( ide->read ( 0x1F0, 2 ) );

		for ( int32_t i = 2; i < 256; i++ )
			ide->read ( 0x1F0, 2 );

		bool extended = first == 0xA100 && second == 0xA101;
		int32_t before = bus->raised;

		out8 ( *ide, 0x1F7, 0x00 );

		bool aborted = ( in8 ( *ide, 0x1F7 ) & 0x01 ) && in8 ( *ide, 0x1F1 ) == 0x04 && bus->raised > before;

		expect ( written && extended && aborted, "modern: IDE PIO writes two sectors, LBA48 reads them back, and unknown commands abort with an interrupt" );

		auto packet = [ & ] ( const uint8_t* command ) {

			out8 ( *ide, 0x176, 0xA0 );
			out8 ( *ide, 0x171, 0 );
			out8 ( *ide, 0x174, 0x00 );
			out8 ( *ide, 0x175, 0x08 );
			out8 ( *ide, 0x177, 0xA0 );

			for ( int32_t i = 0; i < 6; i++ )
				ide->write ( 0x170, 2, static_cast < uint16_t > ( command [ i * 2 ] | command [ i * 2 + 1 ] << 8 ) );

		};

		auto take = [ & ] ( uint8_t* out, uint32_t size ) {

			uint32_t count = static_cast < uint32_t > ( in8 ( *ide, 0x174 ) ) | static_cast < uint32_t > ( in8 ( *ide, 0x175 ) ) << 8;

			for ( uint32_t i = 0; i < count; i += 2 ) {

				uint16_t value = static_cast < uint16_t > ( ide->read ( 0x170, 2 ) );

				if ( i < size )
					out [ i ] = static_cast < uint8_t > ( value );

				if ( i + 1 < size )
					out [ i + 1 ] = static_cast < uint8_t > ( value >> 8 );

			}

			return count;

		};

		uint8_t ready_command [ 12 ] = { 0x00 };
		uint8_t sense_command [ 12 ] = { 0x03, 0, 0, 0, 18 };
		uint8_t capacity_command [ 12 ] = { 0x25 };
		uint8_t read_command [ 12 ] = { 0x28, 0, 0, 0, 0, 3, 0, 0, 1 };
		uint8_t reply [ 2048 ] = { };

		packet ( ready_command );

		bool attention = ( in8 ( *ide, 0x177 ) & 0x01 ) && ( in8 ( *ide, 0x171 ) >> 4 ) == 6;

		packet ( sense_command );
		take ( reply, 18 );

		bool sensed = reply [ 0 ] == 0x70 && reply [ 2 ] == 0x06 && reply [ 12 ] == 0x28;

		packet ( ready_command );

		bool settled = !( in8 ( *ide, 0x177 ) & 0x01 );

		packet ( capacity_command );
		take ( reply, 8 );

		bool capacity = reply [ 3 ] == 9 && reply [ 6 ] == 0x08 && reply [ 7 ] == 0x00;

		packet ( read_command );

		uint32_t got = take ( reply, 2048 );
		uint8_t expected = static_cast < uint8_t > ( ( 3 * 2048 * 7 + 3 + 3 * 2048 / 512 ) & 0xFF );
		bool read = got == 2048 && reply [ 0 ] == expected && in8 ( *ide, 0x172 ) == 3;

		expect ( attention && sensed && settled && capacity && read, "modern: ATAPI reports a disc change once, then answers REQUEST SENSE, READ CAPACITY and READ(10)" );

		if ( hv::present ( ) && vm->create ( 1, 2 * 1024 * 1024 ) ) {

			uint8_t* memory = vm->memory ( );

			for ( uint32_t i = 0; i < 4096; i++ )
				memory [ 0x10000 + i ] = 0;

			memory [ 0x8000 ] = 0x00;
			memory [ 0x8001 ] = 0x00;
			memory [ 0x8002 ] = 0x01;
			memory [ 0x8003 ] = 0x00;
			memory [ 0x8004 ] = 0x00;
			memory [ 0x8005 ] = 0x04;
			memory [ 0x8006 ] = 0x00;
			memory [ 0x8007 ] = 0x80;

			uint16_t master = ide->master_base;

			ide->write ( master + 4, 4, 0x8000 );
			ide->write ( master, 1, 0x08 );
			out8 ( *ide, 0x1F2, 2 );
			out8 ( *ide, 0x1F3, 5 );
			out8 ( *ide, 0x1F4, 0 );
			out8 ( *ide, 0x1F5, 0 );
			out8 ( *ide, 0x1F6, 0xE0 );
			out8 ( *ide, 0x1F7, 0xC8 );
			ide->write ( master, 1, 0x09 );

			uint8_t status = static_cast < uint8_t > ( ide->read ( master + 2, 1 ) );
			bool moved = memory [ 0x10000 ] == 0x00 && memory [ 0x10001 ] == 0xA0 && memory [ 0x10200 ] == 0x00 && memory [ 0x10201 ] == 0xA1;

			expect ( moved && ( status & 0x04 ) && !( status & 0x01 ), "modern: IDE bus-master DMA copies sectors into guest memory and flags the interrupt" );

		}

		delete ide;
		delete disk;
		delete disc;
		delete vm;
		delete bus;
		DeleteFileW ( disk_path );
		DeleteFileW ( disc_path );

	}

	static void smbios_tests ( ) {

		pc::smbios_layout layout;
		pc::smbios_image* image = new pc::smbios_image ( );

		layout.processors = 4;
		layout.low_memory = 0xC0000000ull;
		layout.high_memory = 0x40000000ull;
		layout.seed = 42;
		pc::build_smbios ( layout, *image );

		uint8_t total = 0;
		uint8_t middle = 0;

		for ( int32_t i = 0; i < 31; i++ )
			total = static_cast < uint8_t > ( total + image->entry [ i ] );

		for ( int32_t i = 0x10; i < 0x1F; i++ )
			middle = static_cast < uint8_t > ( middle + image->entry [ i ] );

		const uint8_t* data = image->table.data ( );
		uint32_t size = static_cast < uint32_t > ( image->table.size ( ) );
		uint32_t at = 0;
		int32_t walked = 0;
		int32_t processors = 0;
		int32_t mapped = 0;
		uint8_t last = 0;

		while ( at + 4 <= size ) {

			last = data [ at ];
			processors += last == 4 ? 1 : 0;
			mapped += last == 19 ? 1 : 0;
			at += data [ at + 1 ];

			while ( at + 1 < size && ( data [ at ] || data [ at + 1 ] ) )
				at++;

			at += 2;
			walked++;

			if ( last == 127 )
				break;

		}

		uint32_t length = static_cast < uint32_t > ( image->entry [ 0x16 ] ) | static_cast < uint32_t > ( image->entry [ 0x17 ] ) << 8;
		uint32_t count = static_cast < uint32_t > ( image->entry [ 0x1C ] ) | static_cast < uint32_t > ( image->entry [ 0x1D ] ) << 8;

		expect ( total == 0 && middle == 0 && at == size && length == size && last == 127 && walked == static_cast < int32_t > ( count ) && processors == 4 && mapped == 2, "modern: SMBIOS checksums, walks to its end marker and lists every processor and both memory ranges" );

		delete image;

	}

	static void video_tests ( ) {

		bool unique = true;

		for ( int32_t i = 0; i < pc::vga::mode_count; i++ ) {

			for ( int32_t j = i + 1; j < pc::vga::mode_count; j++ )
				unique = unique && pc::vga::modes [ i ].number != pc::vga::modes [ j ].number;

			const pc::vbe_mode& mode = pc::vga::modes [ i ];

			unique = unique && static_cast < uint64_t > ( mode.width ) * mode.height * ( ( mode.bits + 7u ) / 8u ) <= pc::vga::vram_size && mode.width <= pc::vga::max_width && mode.height <= pc::vga::max_height;

		}

		const pc::vbe_mode* standard = pc::vga::find_mode ( 0x4118 );

		expect ( unique && standard && standard->width == 1024 && standard->bits == 32 && !pc::vga::find_mode ( 0x1FF ), "modern: every VBE mode is unique, fits in video memory, and LFB flags are ignored when looking one up" );

	}

	struct disc_run {

		pc::computer* machine = nullptr;
		char console [ 256 ] = { };
		wchar_t folder [ MAX_PATH ] = { };
		wchar_t boot_path [ MAX_PATH ] = { };
		wchar_t note_path [ MAX_PATH ] = { };
		wchar_t iso_path [ MAX_PATH ] = { };

		~disc_run ( ) {

			if ( machine ) {

				machine->power_off ( );
				delete machine;

			}

			DeleteFileW ( boot_path );
			DeleteFileW ( note_path );
			DeleteFileW ( iso_path );
			RemoveDirectoryW ( folder );

		}

	};

	static bool boot_disc ( disc_run& run, const char* source, int32_t length, const wchar_t* name, int32_t wanted ) {

		assembler::result* image = new assembler::result ( );
		bool staged = assembler::assemble ( source, length, nullptr, 0, *image );
		base::writer path;

		temporary_path ( run.folder, name );
		CreateDirectoryW ( run.folder, nullptr );
		path.add ( run.folder ).add ( L"\\boot.bin" );

		for ( int32_t i = 0; i <= path.size ( ); i++ )
			run.boot_path [ i ] = path.text ( ) [ i ];

		path.clear ( );
		path.add ( run.folder ).add ( L"\\readme.txt" );

		for ( int32_t i = 0; i <= path.size ( ); i++ )
			run.note_path [ i ] = path.text ( ) [ i ];

		path.clear ( );
		path.add ( run.folder ).add ( L".iso" );

		for ( int32_t i = 0; i <= path.size ( ); i++ )
			run.iso_path [ i ] = path.text ( ) [ i ];

		staged = staged && files::write ( run.boot_path, reinterpret_cast < const char* > ( image->bytes.data ( ) ), image->bytes.count ( ) ) && files::write ( run.note_path, "Coldboot test disc", 18 );
		delete image;

		pc::iso_builder* builder = new pc::iso_builder ( );
		char error [ 160 ] = { };

		builder->set_label ( "COLDTEST" );
		staged = staged && builder->add_file ( "boot/boot.bin", run.boot_path ) && builder->add_file ( "readme.txt", run.note_path );
		builder->set_boot ( "boot/boot.bin", 4 );
		staged = staged && builder->write ( run.iso_path, error, sizeof ( error ) );
		delete builder;

		pc::computer_options* options = new pc::computer_options ( );

		options->memory = 64ull * 1024 * 1024;

		for ( int32_t i = 0; run.iso_path [ i ]; i++ )
			options->iso [ i ] = run.iso_path [ i ];

		run.machine = new pc::computer ( );

		bool booted = staged && run.machine->power_on ( *options );

		delete options;

		for ( int32_t waited = 0; booted && waited < 300; waited++ ) {

			if ( run.machine->console_text ( run.console, sizeof ( run.console ) ) >= wanted )
				break;

			Sleep ( 10 );

		}

		Sleep ( 20 );
		run.machine->console_text ( run.console, sizeof ( run.console ) );

		return booted;

	}

	static void boot_tests ( ) {

		if ( !hv::present ( ) ) {

			print ( "  skip  modern: booting an ISO needs the Windows Hypervisor Platform\r\n" );

			return;

		}

		static const char reader [ ] =
			"\torg 0\n"
			"\tmov ax, cs\n"
			"\tmov ds, ax\n"
			"\tmov si, packet\n"
			"\tmov ah, 0x42\n"
			"\tint 0x13\n"
			"\tjc failed\n"
			"\tmov ax, 0x0800\n"
			"\tmov ds, ax\n"
			"\tcmp byte [1], 0x43\n"
			"\tjne failed\n"
			"\tmov al, 0x4F\n"
			"\tout 0xE9, al\n"
			"\tmov al, 0x4B\n"
			"\tout 0xE9, al\n"
			"\tjmp done\n"
			"failed:\n"
			"\tmov al, 0x58\n"
			"\tout 0xE9, al\n"
			"done:\n"
			"\tcli\n"
			"\thlt\n"
			"\tjmp done\n"
			"packet:\n"
			"\tdb 0x10, 0\n"
			"\tdw 1, 0, 0x0800\n"
			"\tdd 16, 0\n";

		disc_run* first = new disc_run ( );
		bool booted = boot_disc ( *first, reader, static_cast < int32_t > ( sizeof ( reader ) - 1 ), L"reader", 2 );
		bool read = booted && first->console [ 0 ] == 'O' && first->console [ 1 ] == 'K' && first->machine->firmware && base::same_text ( first->machine->firmware->last_boot, "dvd" );

		delete first;
		expect ( read, "modern: an ISO from our own builder boots through El Torito, and INT 13h reads its volume descriptor back" );

		static const char painter [ ] =
			"\torg 0\n"
			"\tmov ax, 0x0012\n"
			"\tint 0x10\n"
			"\tmov dx, 0x3CE\n"
			"\tmov ax, 0x0205\n"
			"\tout dx, ax\n"
			"\tmov ax, 0xA000\n"
			"\tmov es, ax\n"
			"\tmov di, 8000\n"
			"\tmov al, 4\n"
			"\tmov cx, 80\n"
			"\trep stosb\n"
			"\tmov ax, 0x0005\n"
			"\tout dx, ax\n"
			"\tmov ah, 0x0C\n"
			"\tmov al, 14\n"
			"\tmov cx, 10\n"
			"\tmov dx, 50\n"
			"\tint 0x10\n"
			"\tmov ah, 0x0D\n"
			"\tmov cx, 10\n"
			"\tmov dx, 50\n"
			"\tint 0x10\n"
			"\tout 0xE9, al\n"
			"\tmov al, 0x4F\n"
			"\tout 0xE9, al\n"
			"done:\n"
			"\tcli\n"
			"\thlt\n"
			"\tjmp done\n";

		disc_run* second = new disc_run ( );
		bool painted = boot_disc ( *second, painter, static_cast < int32_t > ( sizeof ( painter ) - 1 ), L"painter", 2 );
		uint32_t* pixels = static_cast < uint32_t* > ( runtime_allocate ( sizeof ( uint32_t ) * pc::vga::max_width * pc::vga::max_height ) );
		int32_t width = 0;
		int32_t height = 0;

		painted = painted && second->console [ 0 ] == 14 && second->console [ 1 ] == 'O' && second->machine->screen ( pixels, width, height, 0.0 );
		painted = painted && width == 640 && height == 480 && pixels [ 100 * 640 ] == 0xAA0000 && pixels [ 100 * 640 + 639 ] == 0xAA0000 && pixels [ 50 * 640 + 10 ] == 0xFFFF55 && pixels [ 50 * 640 + 11 ] == 0x000000 && pixels [ 101 * 640 ] == 0x000000;

		if ( !painted ) {

			char line [ 256 ] = { };

			append ( line, sizeof ( line ), "  console " );
			append_hex ( line, sizeof ( line ), static_cast < uint8_t > ( second->console [ 0 ] ), 2 );
			append_hex ( line, sizeof ( line ), static_cast < uint8_t > ( second->console [ 1 ] ), 2 );
			append ( line, sizeof ( line ), " size " );
			append_number ( line, sizeof ( line ), width );
			append ( line, sizeof ( line ), "x" );
			append_number ( line, sizeof ( line ), height );
			append ( line, sizeof ( line ), " row100 " );
			append_hex ( line, sizeof ( line ), width ? pixels [ 100 * 640 ] : 0, 6 );
			append ( line, sizeof ( line ), " dot " );
			append_hex ( line, sizeof ( line ), width ? pixels [ 50 * 640 + 10 ] : 0, 6 );
			append ( line, sizeof ( line ), " problem " );
			append ( line, sizeof ( line ), second->machine->board ? second->machine->board->vm.problem : "-" );
			append ( line, sizeof ( line ), "\r\n" );
			print ( line );

		}

		runtime_release ( pixels );
		delete second;
		expect ( painted, "modern: VGA mode 12h takes write-mode-2 stores through the trapped window and INT 10h pixels, and renders them" );

	}


	void modern_tests ( ) {

		print ( "\r\n" );
		aml_tests ( );
		ide_tests ( );
		smbios_tests ( );
		video_tests ( );
		boot_tests ( );

	}

}

#endif