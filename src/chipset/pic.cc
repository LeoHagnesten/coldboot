#include "chipset/pic.hh"
#include "machine/archive.hh"

namespace machine {

	void pic::transfer ( archive& io ) {

		io.field ( irr );
		io.field ( isr );
		io.field ( imr );
		io.field ( vector_base );
		io.field ( lines );
		io.field ( lowest );
		io.field ( init_step );
		io.field ( level );
		io.field ( single );
		io.field ( needs_icw4 );
		io.field ( auto_eoi );
		io.field ( rotate_on_auto_eoi );
		io.field ( special_mask );
		io.field ( read_service );
		io.field ( poll );

	}

	void pic::reset ( ) {

		irr = 0;
		isr = 0;
		imr = 0xFF;
		vector_base = 0x08;
		lines = 0;
		lowest = 7;
		init_step = 0;
		level = false;
		single = true;
		needs_icw4 = false;
		auto_eoi = false;
		rotate_on_auto_eoi = false;
		special_mask = false;
		read_service = false;
		poll = false;

	}

	void pic::set_line ( uint8_t irq, bool high ) {

		uint8_t bit = static_cast < uint8_t > ( 1 << ( irq & 7 ) );
		bool was = ( lines & bit ) != 0;

		if ( high )
			lines = static_cast < uint8_t > ( lines | bit );
		else
			lines = static_cast < uint8_t > ( lines & ~bit );

		if ( level ) {

			irr = high ? static_cast < uint8_t > ( irr | bit ) : static_cast < uint8_t > ( irr & ~bit );

			return;

		}

		if ( high && !was )
			irr = static_cast < uint8_t > ( irr | bit );
		else if ( !high )
			irr = static_cast < uint8_t > ( irr & ~bit );

	}

	int32_t pic::highest_request ( ) const {

		uint8_t requests = static_cast < uint8_t > ( irr & ~imr );

		for ( int32_t step = 1; step <= 8; step++ ) {

			int32_t irq = ( lowest + step ) & 7;
			uint8_t bit = static_cast < uint8_t > ( 1 << irq );

			if ( ( isr & bit ) && !special_mask )
				return -1;

			if ( requests & bit && !( special_mask && ( isr & bit ) ) )
				return irq;

		}

		return -1;

	}

	int32_t pic::highest_service ( ) const {

		for ( int32_t step = 1; step <= 8; step++ ) {

			int32_t irq = ( lowest + step ) & 7;

			if ( isr & ( 1 << irq ) )
				return irq;

		}

		return -1;

	}

	bool pic::pending ( ) {

		return init_step == 0 && highest_request ( ) >= 0;

	}

	uint8_t pic::acknowledge ( ) {

		int32_t irq = highest_request ( );

		if ( irq < 0 )
			return static_cast < uint8_t > ( vector_base | 7 );

		uint8_t bit = static_cast < uint8_t > ( 1 << irq );

		if ( !level )
			irr = static_cast < uint8_t > ( irr & ~bit );

		if ( auto_eoi ) {

			if ( rotate_on_auto_eoi )
				lowest = static_cast < uint8_t > ( irq );

		} else
			isr = static_cast < uint8_t > ( isr | bit );

		return static_cast < uint8_t > ( vector_base | irq );

	}

	uint8_t pic::in ( uint16_t port ) {

		if ( port & 1 )
			return imr;

		if ( poll ) {

			poll = false;

			if ( highest_request ( ) < 0 )
				return 0;

			return static_cast < uint8_t > ( 0x80 | ( acknowledge ( ) & 7 ) );

		}

		return read_service ? isr : irr;

	}

	void pic::out ( uint16_t port, uint8_t value ) {

		if ( port & 1 ) {

			switch ( init_step ) {

				case 1:
					vector_base = static_cast < uint8_t > ( value & 0xF8 );
					init_step = single ? ( needs_icw4 ? 3 : 0 ) : 2;
					return;

				case 2:
					init_step = needs_icw4 ? 3 : 0;
					return;

				case 3:
					auto_eoi = ( value & 0x02 ) != 0;
					init_step = 0;
					return;

				default:
					imr = value;
					return;

			}

		}

		if ( value & 0x10 ) {

			level = ( value & 0x08 ) != 0;
			single = ( value & 0x02 ) != 0;
			needs_icw4 = ( value & 0x01 ) != 0;
			auto_eoi = false;
			rotate_on_auto_eoi = false;
			special_mask = false;
			read_service = false;
			poll = false;
			imr = 0;
			isr = 0;
			irr = level ? lines : 0;
			lowest = 7;
			init_step = 1;

			return;

		}

		if ( value & 0x08 ) {

			if ( value & 0x02 )
				read_service = ( value & 0x01 ) != 0;

			if ( value & 0x40 )
				special_mask = ( value & 0x20 ) != 0;

			poll = ( value & 0x04 ) != 0;

			return;

		}

		uint8_t command = static_cast < uint8_t > ( value >> 5 );
		uint8_t target = static_cast < uint8_t > ( value & 7 );

		switch ( command ) {

			case 1:
			case 5: {

				int32_t irq = highest_service ( );

				if ( irq >= 0 ) {

					isr = static_cast < uint8_t > ( isr & ~( 1 << irq ) );

					if ( command == 5 )
						lowest = static_cast < uint8_t > ( irq );

				}

				return;

			}

			case 3:
			case 7:
				isr = static_cast < uint8_t > ( isr & ~( 1 << target ) );

				if ( command == 7 )
					lowest = target;

				return;

			case 4:
				rotate_on_auto_eoi = true;
				return;

			case 0:
				rotate_on_auto_eoi = false;
				return;

			case 6:
				lowest = target;
				return;

			default:
				return;

		}

	}

}