#include "pc/acpi.hh"
#include "pc/aml.hh"
#include "pc/ioapic.hh"
#include "pc/peripherals.hh"

namespace pc {

	class table_writer {

	public:

		explicit table_writer ( base::list < uint8_t >& target ) : out ( target ) { }

		uint32_t align ( uint32_t boundary ) {

			while ( out.size ( ) % boundary )
				out.push ( 0 );

			return static_cast < uint32_t > ( out.size ( ) );

		}

		uint32_t begin ( const char* signature, uint8_t revision ) {

			uint32_t start = align ( 16 );

			for ( int32_t i = 0; i < 4; i++ )
				u8 ( static_cast < uint8_t > ( signature [ i ] ) );

			u32 ( 0 );
			u8 ( revision );
			u8 ( 0 );
			text ( "COLDBT", 6 );
			text ( "COLDBOOT", 8 );
			u32 ( 1 );
			text ( "CBVM", 4 );
			u32 ( 1 );

			return start;

		}

		void end ( uint32_t start ) {

			uint32_t length = static_cast < uint32_t > ( out.size ( ) ) - start;
			uint8_t* data = out.data ( ) + start;

			data [ 4 ] = static_cast < uint8_t > ( length );
			data [ 5 ] = static_cast < uint8_t > ( length >> 8 );
			data [ 6 ] = static_cast < uint8_t > ( length >> 16 );
			data [ 7 ] = static_cast < uint8_t > ( length >> 24 );
			data [ 9 ] = 0;
			data [ 9 ] = static_cast < uint8_t > ( 0x100 - acpi_sum ( data, length ) );

		}

		void u8 ( uint8_t value ) {

			out.push ( value );

		}

		void u16 ( uint16_t value ) {

			u8 ( static_cast < uint8_t > ( value ) );
			u8 ( static_cast < uint8_t > ( value >> 8 ) );

		}

		void u32 ( uint32_t value ) {

			u16 ( static_cast < uint16_t > ( value ) );
			u16 ( static_cast < uint16_t > ( value >> 16 ) );

		}

		void u64 ( uint64_t value ) {

			u32 ( static_cast < uint32_t > ( value ) );
			u32 ( static_cast < uint32_t > ( value >> 32 ) );

		}

		void text ( const char* value, int32_t length ) {

			int32_t i = 0;

			for ( ; value [ i ] && i < length; i++ )
				u8 ( static_cast < uint8_t > ( value [ i ] ) );

			for ( ; i < length; i++ )
				u8 ( ' ' );

		}

		void zeros ( uint32_t count ) {

			for ( uint32_t i = 0; i < count; i++ )
				u8 ( 0 );

		}

		void address ( uint8_t space, uint8_t bits, uint8_t access, uint64_t where ) {

			u8 ( space );
			u8 ( bits );
			u8 ( 0 );
			u8 ( access );
			u64 ( where );

		}

	private:

		base::list < uint8_t >& out;

	};

	uint8_t acpi_sum ( const uint8_t* data, uint32_t length ) {

		uint8_t sum = 0;

		for ( uint32_t i = 0; i < length; i++ )
			sum = static_cast < uint8_t > ( sum + data [ i ] );

		return sum;

	}

	static void small_device ( aml& code, const char* name, const char* id, resources& claimed, int32_t unique = -1 ) {

		int32_t marker = code.device ( name );

		code.name ( "_HID" );
		code.eisa ( id );

		if ( unique >= 0 ) {

			code.name ( "_UID" );
			code.integer ( static_cast < uint64_t > ( unique ) );

		}

		code.name ( "_CRS" );
		claimed.emit ( code );
		code.close ( marker );

	}

	void dsdt_body ( const acpi_layout& layout, base::list < uint8_t >& out ) {

		aml code;
		int32_t root = code.scope ( "\\_SB" );

		for ( uint32_t i = 0; i < layout.processors; i++ ) {

			static const char digits [ ] = "0123456789ABCDEF";
			char name [ 5 ] = { 'C', 'P', digits [ i >> 4 & 15 ], digits [ i & 15 ], 0 };

			code.processor ( name, static_cast < uint8_t > ( i ) );

		}

		int32_t bridge = code.device ( "PCI0" );

		code.name ( "_HID" );
		code.eisa ( "PNP0A03" );
		code.name ( "_ADR" );
		code.integer ( 0 );
		code.name ( "_UID" );
		code.integer ( 0 );
		code.name ( "_BBN" );
		code.integer ( 0 );

		uint64_t window = layout.low_memory < 0x80000000ull ? 0x80000000ull : layout.low_memory;
		resources windows;

		windows.bus_range ( 0, 0xFF );
		windows.io ( 0xCF8, 8 );
		windows.io_range ( 0x0000, 0x0CF7 );
		windows.io_range ( 0x0D00, 0xFFFF );
		windows.memory_range ( 0x000A0000, 0x000BFFFF, true );
		windows.memory_range ( static_cast < uint32_t > ( window ), 0xFEBFFFFF, false );

		uint64_t above = 0x100000000ull + layout.high_memory;

		above = ( above + 0x3FFFFFFFull ) & ~0x3FFFFFFFull;
		windows.memory_range_64 ( above, above + 0x7FFFFFFFFull );
		code.name ( "_CRS" );
		windows.emit ( code );

		code.name ( "_PRT" );

		int32_t routing = code.package ( static_cast < uint8_t > ( 13 * 4 ) );

		for ( uint32_t slot = 3; slot < 16; slot++ ) {

			for ( uint32_t pin = 0; pin < 4; pin++ ) {

				int32_t entry = code.package ( 4 );

				code.integer ( slot << 16 | 0xFFFF );
				code.integer ( pin );
				code.integer ( 0 );
				code.integer ( 16 + ( ( slot + pin ) & 3 ) );
				code.close ( entry );

			}

		}

		code.close ( routing );

		int32_t isa = code.device ( "ISA" );

		code.name ( "_ADR" );
		code.integer ( 0x00010000 );

		{

			resources claimed;

			claimed.io ( 0x60, 1 );
			claimed.io ( 0x64, 1 );
			claimed.irq ( 1u << 1 );
			small_device ( code, "KBD", "PNP0303", claimed );

		}

		{

			resources claimed;

			claimed.irq ( 1u << 12 );
			small_device ( code, "MOU", "PNP0F13", claimed );

		}

		{

			resources claimed;

			claimed.io ( 0x70, 2 );
			claimed.irq ( 1u << 8 );
			small_device ( code, "RTC", "PNP0B00", claimed );

		}

		{

			resources claimed;

			claimed.io ( 0x40, 4 );
			claimed.irq ( 1u << 0 );
			small_device ( code, "TMR", "PNP0100", claimed );

		}

		{

			resources claimed;

			claimed.io ( 0x20, 2 );
			claimed.io ( 0xA0, 2 );
			claimed.io ( 0x4D0, 2 );
			claimed.irq ( 1u << 2 );
			small_device ( code, "PIC", "PNP0000", claimed );

		}

		{

			resources claimed;

			claimed.io ( 0x61, 1 );
			small_device ( code, "SPKR", "PNP0800", claimed );

		}

		{

			resources claimed;

			claimed.io ( 0x3F8, 8 );
			claimed.irq ( 1u << 4 );
			small_device ( code, "COM1", "PNP0501", claimed, 1 );

		}

		{

			resources claimed;

			claimed.io ( power_management::base, 0x28 );
			claimed.io ( 0x0E00, 8 );
			claimed.io ( 0x92, 1 );
			claimed.io ( 0x80, 1 );
			claimed.io ( 0xE9, 1 );
			small_device ( code, "SYS", "PNP0C02", claimed, 2 );

		}

		code.close ( isa );

		int32_t disk = code.device ( "IDE0" );

		code.name ( "_ADR" );
		code.integer ( 0x00010001 );
		code.close ( disk );

		int32_t screen = code.device ( "GFX0" );

		code.name ( "_ADR" );
		code.integer ( 0x00020000 );
		code.close ( screen );
		code.close ( bridge );
		code.close ( root );

		code.name ( "\\_S0" );

		int32_t awake = code.package ( 4 );

		code.integer ( 0 );
		code.integer ( 0 );
		code.integer ( 0 );
		code.integer ( 0 );
		code.close ( awake );

		code.name ( "\\_S5" );

		int32_t off = code.package ( 4 );

		code.integer ( power_management::sleep_off );
		code.integer ( power_management::sleep_off );
		code.integer ( 0 );
		code.integer ( 0 );
		code.close ( off );

		int32_t mode = code.method ( "\\_PIC", 1, false );

		code.close ( mode );
		out = base::move ( code.bytes );

	}

	void build_acpi ( const acpi_layout& layout, acpi_image& image ) {

		base::list < uint8_t >& out = image.tables;
		table_writer write ( out );

		out.clear ( );

		uint32_t facs = write.align ( 64 );

		write.text ( "FACS", 4 );
		write.u32 ( 64 );
		write.u32 ( 0 );
		write.u32 ( 0 );
		write.u32 ( 0 );
		write.u32 ( 0 );
		write.u64 ( 0 );
		write.u8 ( 1 );
		write.zeros ( 31 );

		base::list < uint8_t > body;

		dsdt_body ( layout, body );

		uint32_t dsdt = write.begin ( "DSDT", 2 );

		for ( uint8_t value : body )
			write.u8 ( value );

		write.end ( dsdt );

		uint32_t fadt = write.begin ( "FACP", 3 );
		uint32_t pm = power_management::base;

		write.u32 ( layout.tables + facs );
		write.u32 ( layout.tables + dsdt );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u16 ( static_cast < uint16_t > ( power_management::sci ) );
		write.u32 ( 0 );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u32 ( pm );
		write.u32 ( 0 );
		write.u32 ( pm + 4 );
		write.u32 ( 0 );
		write.u32 ( 0 );
		write.u32 ( pm + 8 );
		write.u32 ( pm + 0x20 );
		write.u32 ( 0 );
		write.u8 ( 4 );
		write.u8 ( 2 );
		write.u8 ( 0 );
		write.u8 ( 4 );
		write.u8 ( 8 );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u16 ( 0x0FFF );
		write.u16 ( 0x0FFF );
		write.u16 ( 0 );
		write.u16 ( 0 );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u8 ( 0x32 );
		write.u16 ( 0x0003 );
		write.u8 ( 0 );
		write.u32 ( ( 1u << 0 ) | ( 1u << 2 ) | ( 1u << 5 ) | ( 1u << 6 ) | ( 1u << 8 ) | ( 1u << 10 ) );
		write.address ( 1, 8, 1, 0xCF9 );
		write.u8 ( 0x06 );
		write.zeros ( 3 );
		write.u64 ( layout.tables + facs );
		write.u64 ( layout.tables + dsdt );
		write.address ( 1, 32, 2, pm );
		write.address ( 0, 0, 0, 0 );
		write.address ( 1, 16, 2, pm + 4 );
		write.address ( 0, 0, 0, 0 );
		write.address ( 0, 0, 0, 0 );
		write.address ( 1, 32, 3, pm + 8 );
		write.address ( 1, 64, 1, pm + 0x20 );
		write.address ( 0, 0, 0, 0 );
		write.end ( fadt );

		uint32_t madt = write.begin ( "APIC", 1 );

		write.u32 ( 0xFEE00000 );
		write.u32 ( 1 );

		for ( uint32_t i = 0; i < layout.processors; i++ ) {

			write.u8 ( 0 );
			write.u8 ( 8 );
			write.u8 ( static_cast < uint8_t > ( i ) );
			write.u8 ( static_cast < uint8_t > ( i ) );
			write.u32 ( 1 );

		}

		write.u8 ( 1 );
		write.u8 ( 12 );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u32 ( static_cast < uint32_t > ( ioapic::base ) );
		write.u32 ( 0 );
		write.u8 ( 2 );
		write.u8 ( 10 );
		write.u8 ( 0 );
		write.u8 ( 0 );
		write.u32 ( 2 );
		write.u16 ( 0 );
		write.u8 ( 2 );
		write.u8 ( 10 );
		write.u8 ( 0 );
		write.u8 ( static_cast < uint8_t > ( power_management::sci ) );
		write.u32 ( power_management::sci );
		write.u16 ( 0x000D );
		write.u8 ( 4 );
		write.u8 ( 6 );
		write.u8 ( 0xFF );
		write.u16 ( 0 );
		write.u8 ( 1 );
		write.end ( madt );

		uint32_t rsdt = write.begin ( "RSDT", 1 );

		write.u32 ( layout.tables + fadt );
		write.u32 ( layout.tables + madt );
		write.end ( rsdt );

		uint32_t xsdt = write.begin ( "XSDT", 1 );

		write.u64 ( layout.tables + fadt );
		write.u64 ( layout.tables + madt );
		write.end ( xsdt );

		uint8_t* pointer = image.rsdp;
		const char* signature = "RSD PTR COLDBT";

		for ( int32_t i = 0; i < 36; i++ )
			pointer [ i ] = 0;

		for ( int32_t i = 0; i < 8; i++ )
			pointer [ i ] = static_cast < uint8_t > ( signature [ i ] );

		for ( int32_t i = 0; i < 6; i++ )
			pointer [ 9 + i ] = static_cast < uint8_t > ( signature [ 8 + i ] );

		uint32_t rsdt_address = layout.tables + rsdt;
		uint64_t xsdt_address = layout.tables + xsdt;

		pointer [ 15 ] = 2;

		for ( int32_t i = 0; i < 4; i++ )
			pointer [ 16 + i ] = static_cast < uint8_t > ( rsdt_address >> ( i * 8 ) );

		pointer [ 20 ] = 36;

		for ( int32_t i = 0; i < 8; i++ )
			pointer [ 24 + i ] = static_cast < uint8_t > ( xsdt_address >> ( i * 8 ) );

		pointer [ 8 ] = static_cast < uint8_t > ( 0x100 - acpi_sum ( pointer, 20 ) );
		pointer [ 32 ] = static_cast < uint8_t > ( 0x100 - acpi_sum ( pointer, 36 ) );

	}

}