#include "pc/peripherals.hh"

namespace pc {

	void power_management::reset ( ) {

		status = 0;
		enable = 0;
		control = 1;
		gpe_status = 0;
		gpe_enable = 0;
		origin = bus.now ( );
		high_bit = false;
		line = false;

	}

	uint32_t power_management::timer ( ) const {

		uint64_t elapsed = bus.now ( ) - origin;

		return static_cast < uint32_t > ( elapsed / 1000000000ull * frequency + elapsed % 1000000000ull * frequency / 1000000000ull );

	}

	void power_management::update ( ) {

		bool active = ( control & 1 ) && ( ( status & enable & 0x0721 ) || ( gpe_status & gpe_enable ) );

		if ( active != line ) {

			line = active;
			bus.set_irq ( sci, line );

		}

	}

	void power_management::advance ( ) {

		bool top = ( timer ( ) >> 31 ) != 0;

		if ( top != high_bit ) {

			high_bit = top;
			status |= 0x0001;

		}

		update ( );

	}

	void power_management::press_power ( ) {

		status |= 0x0100;
		update ( );

	}

	uint8_t power_management::byte_at ( uint32_t offset ) const {

		uint32_t value = 0;
		uint32_t shift = ( offset & 1 ) * 8;

		if ( offset < 2 )
			value = status >> shift;
		else if ( offset < 4 )
			value = enable >> shift;
		else if ( offset < 6 )
			value = ( control & ~0x2000 ) >> shift;
		else if ( offset >= 8 && offset < 12 )
			value = timer ( ) >> ( ( offset - 8 ) * 8 );
		else if ( offset >= 0x20 && offset < 0x24 )
			value = gpe_status >> ( ( offset - 0x20 ) * 8 );
		else if ( offset >= 0x24 && offset < 0x28 )
			value = gpe_enable >> ( ( offset - 0x24 ) * 8 );

		return static_cast < uint8_t > ( value );

	}

	uint64_t power_management::read ( uint64_t port, uint32_t size ) {

		uint32_t offset = static_cast < uint32_t > ( port - base );
		uint64_t value = 0;

		if ( offset == 8 && size == 4 )
			return timer ( );

		for ( uint32_t i = 0; i < size; i++ )
			value |= static_cast < uint64_t > ( byte_at ( offset + i ) ) << ( i * 8 );

		return value;

	}

	void power_management::write ( uint64_t port, uint32_t size, uint64_t value ) {

		uint32_t offset = static_cast < uint32_t > ( port - base );

		for ( uint32_t i = 0; i < size; i++ ) {

			uint32_t at = offset + i;
			uint32_t byte = static_cast < uint32_t > ( value >> ( i * 8 ) ) & 0xFF;
			uint32_t shift = ( at & 1 ) * 8;

			if ( at < 2 )
				status = static_cast < uint16_t > ( status & ~( byte << shift ) );
			else if ( at < 4 )
				enable = static_cast < uint16_t > ( ( enable & ~( 0xFF << shift ) ) | byte << shift );
			else if ( at < 6 )
				control = static_cast < uint16_t > ( ( ( control & ~( 0xFF << shift ) ) | byte << shift ) | 1 );
			else if ( at >= 0x20 && at < 0x24 )
				gpe_status &= ~( byte << ( ( at - 0x20 ) * 8 ) );
			else if ( at >= 0x24 && at < 0x28 )
				gpe_enable = ( gpe_enable & ~( 0xFFu << ( ( at - 0x24 ) * 8 ) ) ) | byte << ( ( at - 0x24 ) * 8 );

		}

		if ( control & 0x2000 ) {

			uint32_t kind = control >> 10 & 7;

			control &= ~0x2000;

			if ( kind == sleep_off )
				bus.request_power_off ( );
			else
				status |= 0x8000;

		}

		update ( );

	}

	void serial_port::reset ( ) {

		incoming.clear ( );
		interrupts = 0;
		identity = 0x01;
		format = 0x03;
		modem = 0;
		scratch = 0;
		fifo = 0;
		divisor [ 0 ] = 12;
		divisor [ 1 ] = 0;
		empty_pending = false;
		line = false;

	}

	void serial_port::update ( ) {

		if ( ( interrupts & 0x01 ) && incoming.count )
			identity = 0x04;
		else if ( ( interrupts & 0x02 ) && empty_pending )
			identity = 0x02;
		else
			identity = 0x01;

		bool active = identity != 0x01 && ( modem & 0x08 );

		if ( active != line ) {

			line = active;
			bus.set_irq ( line_number, line );

		}

	}

	void serial_port::receive ( uint8_t value ) {

		incoming.push ( value );
		update ( );

	}

	uint64_t serial_port::read ( uint64_t port, uint32_t ) {

		uint32_t offset = static_cast < uint32_t > ( port - base );
		bool latch = ( format & 0x80 ) != 0;

		switch ( offset ) {

			case 0:

				if ( latch )
					return divisor [ 0 ];

				if ( incoming.count ) {

					uint8_t value = incoming.pop ( );

					update ( );

					return value;

				}

				return 0;

			case 1:
				return latch ? divisor [ 1 ] : interrupts;

			case 2: {

				uint8_t value = static_cast < uint8_t > ( identity | ( fifo & 1 ? 0xC0 : 0 ) );

				if ( identity == 0x02 ) {

					empty_pending = false;
					update ( );

				}

				return value;

			}

			case 3:
				return format;

			case 4:
				return modem;

			case 5:
				return 0x60 | ( incoming.count ? 0x01 : 0 );

			case 6:
				return modem & 0x10 ? static_cast < uint8_t > ( ( modem & 0x0C ) << 4 | ( modem & 0x01 ) << 5 | ( modem & 0x02 ) << 3 ) : 0xB0;

			default:
				return scratch;

		}

	}

	void serial_port::write ( uint64_t port, uint32_t, uint64_t raw ) {

		uint32_t offset = static_cast < uint32_t > ( port - base );
		uint8_t value = static_cast < uint8_t > ( raw );
		bool latch = ( format & 0x80 ) != 0;

		switch ( offset ) {

			case 0:

				if ( latch ) {

					divisor [ 0 ] = value;

					return;

				}

				if ( modem & 0x10 )
					incoming.push ( value );
				else
					bus.console ( static_cast < char > ( value ) );

				empty_pending = true;
				update ( );

				return;

			case 1:

				if ( latch ) {

					divisor [ 1 ] = value;

					return;

				}

				interrupts = static_cast < uint8_t > ( value & 0x0F );

				if ( interrupts & 0x02 )
					empty_pending = true;

				update ( );

				return;

			case 2:
				fifo = value;

				if ( value & 0x02 )
					incoming.clear ( );

				update ( );

				return;

			case 3:
				format = value;
				return;

			case 4:
				modem = static_cast < uint8_t > ( value & 0x1F );
				update ( );
				return;

			case 7:
				scratch = value;
				return;

			default:
				return;

		}

	}

}