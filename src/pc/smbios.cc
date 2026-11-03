#include "pc/smbios.hh"

namespace pc {

	class smbios_writer {

	public:

		explicit smbios_writer ( smbios_image& target ) : image ( target ) { }

		void begin ( uint8_t type, uint8_t length ) {

			start = static_cast < uint32_t > ( image.table.size ( ) );
			strings = 0;
			u8 ( type );
			u8 ( length );
			u16 ( handle++ );

		}

		void u8 ( uint8_t value ) {

			image.table.push ( value );

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

		uint8_t text ( const char* value ) {

			if ( !value || !value [ 0 ] ) {

				u8 ( 0 );

				return 0;

			}

			pending [ strings ] = value;
			strings++;
			u8 ( static_cast < uint8_t > ( strings ) );

			return strings;

		}

		void end ( ) {

			uint8_t length = image.table [ start + 1 ];

			while ( image.table.size ( ) < start + length )
				u8 ( 0 );

			for ( uint8_t i = 0; i < strings; i++ ) {

				for ( int32_t c = 0; pending [ i ] [ c ]; c++ )
					u8 ( static_cast < uint8_t > ( pending [ i ] [ c ] ) );

				u8 ( 0 );

			}

			if ( !strings )
				u8 ( 0 );

			u8 ( 0 );

			uint32_t size = static_cast < uint32_t > ( image.table.size ( ) ) - start;

			if ( size > largest )
				largest = size;

			image.structures++;

		}

		uint16_t next_handle ( ) const {

			return handle;

		}

		uint32_t largest = 0;

	private:

		smbios_image& image;
		uint32_t start = 0;
		uint16_t handle = 0;
		uint8_t strings = 0;
		const char* pending [ 8 ] = { };

	};

	static uint8_t sum ( const uint8_t* data, uint32_t length ) {

		uint8_t total = 0;

		for ( uint32_t i = 0; i < length; i++ )
			total = static_cast < uint8_t > ( total + data [ i ] );

		return total;

	}

	void build_smbios ( const smbios_layout& layout, smbios_image& out ) {

		smbios_writer write ( out );
		uint64_t total = layout.low_memory + layout.high_memory;
		uint64_t mixed = layout.seed * 0x9E3779B97F4A7C15ull ^ 0xC01DB007C01DB007ull;
		static char socket_names [ 64 ] [ 8 ];

		out.table.clear ( );
		out.structures = 0;

		write.begin ( 0, 0x18 );
		write.text ( "Coldboot" );
		write.text ( "1.0" );
		write.u16 ( 0xE800 );
		write.text ( "11/02/2026" );
		write.u8 ( 0 );
		write.u64 ( 0x0000000000000088ull );
		write.u8 ( 0x01 );
		write.u8 ( 0x10 );
		write.u8 ( 1 );
		write.u8 ( 0 );
		write.u8 ( 0xFF );
		write.u8 ( 0xFF );
		write.end ( );

		write.begin ( 1, 0x1B );
		write.text ( "Coldboot" );
		write.text ( "Coldboot Virtual Machine" );
		write.text ( "1.0" );
		write.text ( "CB-0001" );

		for ( int32_t i = 0; i < 16; i++ ) {

			mixed = mixed * 6364136223846793005ull + 1442695040888963407ull;
			write.u8 ( static_cast < uint8_t > ( mixed >> 56 ) );

		}

		write.u8 ( 0x06 );
		write.text ( "" );
		write.text ( "Virtual Machine" );
		write.end ( );

		write.begin ( 3, 0x11 );
		write.text ( "Coldboot" );
		write.u8 ( 0x01 );
		write.text ( "1.0" );
		write.text ( "" );
		write.text ( "" );
		write.u8 ( 0x03 );
		write.u8 ( 0x03 );
		write.u8 ( 0x03 );
		write.u8 ( 0x02 );
		write.u32 ( 0 );
		write.end ( );

		for ( uint32_t i = 0; i < layout.processors && i < 64; i++ ) {

			char* name = socket_names [ i ];

			name [ 0 ] = 'C';
			name [ 1 ] = 'P';
			name [ 2 ] = 'U';
			name [ 3 ] = ' ';
			name [ 4 ] = static_cast < char > ( i >= 10 ? '0' + i / 10 : '0' + i );
			name [ 5 ] = static_cast < char > ( i >= 10 ? '0' + i % 10 : 0 );
			name [ 6 ] = 0;

			write.begin ( 4, 0x28 );
			write.text ( name );
			write.u8 ( 0x03 );
			write.u8 ( 0x01 );
			write.text ( "Coldboot" );
			write.u64 ( 0 );
			write.text ( "Virtual processor" );
			write.u8 ( 0 );
			write.u16 ( 0 );
			write.u16 ( 2000 );
			write.u16 ( 2000 );
			write.u8 ( 0x41 );
			write.u8 ( 0x01 );
			write.u16 ( 0xFFFF );
			write.u16 ( 0xFFFF );
			write.u16 ( 0xFFFF );
			write.text ( "" );
			write.text ( "" );
			write.text ( "" );
			write.u8 ( 1 );
			write.u8 ( 1 );
			write.u8 ( 1 );
			write.u16 ( 0x0004 );
			write.end ( );

		}

		uint16_t array = write.next_handle ( );
		uint64_t kilobytes = total / 1024;

		write.begin ( 16, 0x0F );
		write.u8 ( 0x03 );
		write.u8 ( 0x03 );
		write.u8 ( 0x03 );
		write.u32 ( kilobytes >= 0x80000000ull ? 0x80000000u : static_cast < uint32_t > ( kilobytes ) );
		write.u16 ( 0xFFFE );
		write.u16 ( 1 );
		write.end ( );

		uint64_t megabytes = total / ( 1024 * 1024 );

		write.begin ( 17, 0x15 );
		write.u16 ( array );
		write.u16 ( 0xFFFE );
		write.u16 ( 64 );
		write.u16 ( 64 );
		write.u16 ( static_cast < uint16_t > ( megabytes >= 0x7FFF ? 0x7FFF : megabytes ) );
		write.u8 ( 0x09 );
		write.u8 ( 0 );
		write.text ( "DIMM 0" );
		write.text ( "" );
		write.u8 ( 0x02 );
		write.u16 ( 0x0080 );
		write.end ( );

		auto mapped = [ & ] ( uint64_t start, uint64_t size ) {

			if ( !size )
				return;

			write.begin ( 19, 0x0F );
			write.u32 ( static_cast < uint32_t > ( start / 1024 ) );
			write.u32 ( static_cast < uint32_t > ( ( start + size ) / 1024 - 1 ) );
			write.u16 ( array );
			write.u8 ( 1 );
			write.end ( );

		};

		mapped ( 0, layout.low_memory );
		mapped ( 0x100000000ull, layout.high_memory );

		write.begin ( 32, 0x0B );

		for ( int32_t i = 0; i < 6; i++ )
			write.u8 ( 0 );

		write.u8 ( 0 );
		write.end ( );

		write.begin ( 127, 0x04 );
		write.end ( );

		uint8_t* entry = out.entry;
		const char* anchor = "_SM_";
		const char* middle = "_DMI_";

		for ( int32_t i = 0; i < 31; i++ )
			entry [ i ] = 0;

		for ( int32_t i = 0; i < 4; i++ )
			entry [ i ] = static_cast < uint8_t > ( anchor [ i ] );

		entry [ 5 ] = 0x1F;
		entry [ 6 ] = 2;
		entry [ 7 ] = 8;
		entry [ 8 ] = static_cast < uint8_t > ( write.largest );
		entry [ 9 ] = static_cast < uint8_t > ( write.largest >> 8 );

		for ( int32_t i = 0; i < 5; i++ )
			entry [ 0x10 + i ] = static_cast < uint8_t > ( middle [ i ] );

		uint32_t length = static_cast < uint32_t > ( out.table.size ( ) );

		entry [ 0x16 ] = static_cast < uint8_t > ( length );
		entry [ 0x17 ] = static_cast < uint8_t > ( length >> 8 );
		entry [ 0x18 ] = static_cast < uint8_t > ( layout.table_address );
		entry [ 0x19 ] = static_cast < uint8_t > ( layout.table_address >> 8 );
		entry [ 0x1A ] = static_cast < uint8_t > ( layout.table_address >> 16 );
		entry [ 0x1B ] = static_cast < uint8_t > ( layout.table_address >> 24 );
		entry [ 0x1C ] = static_cast < uint8_t > ( out.structures );
		entry [ 0x1D ] = static_cast < uint8_t > ( out.structures >> 8 );
		entry [ 0x1E ] = 0x28;
		entry [ 0x15 ] = static_cast < uint8_t > ( 0x100 - sum ( entry + 0x10, 15 ) );
		entry [ 4 ] = static_cast < uint8_t > ( 0x100 - sum ( entry, 31 ) );

	}

}