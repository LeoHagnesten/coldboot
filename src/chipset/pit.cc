#include "chipset/pit.hh"
#include "machine/archive.hh"

namespace machine {

	void pit::transfer ( archive& io ) {

		io.field ( channels );

	}

	static uint32_t from_bcd ( uint16_t value ) {

		return ( value & 0xF ) + ( ( value >> 4 ) & 0xF ) * 10 + ( ( value >> 8 ) & 0xF ) * 100 + ( ( value >> 12 ) & 0xF ) * 1000;

	}

	static uint16_t to_bcd ( uint32_t value ) {

		value %= 10000;

		return static_cast < uint16_t > ( value % 10 | ( value / 10 % 10 ) << 4 | ( value / 100 % 10 ) << 8 | ( value / 1000 ) << 12 );

	}

	void pit_channel::load ( ) {

		if ( mode == 3 )
			count = reload & ~1u;
		else
			count = reload;

		counting = true;
		odd_wait = false;

	}

	uint16_t pit_channel::visible ( ) const {

		uint32_t value = count;

		if ( bcd )
			return to_bcd ( value );

		return static_cast < uint16_t > ( value );

	}

	void pit_channel::clock ( ) {

		uint32_t wrap = bcd ? 10000 : 0x10000;

		if ( load_pending ) {

			load_pending = false;
			load ( );

			return;

		}

		if ( trigger_pending ) {

			trigger_pending = false;
			load ( );

			if ( mode == 1 )
				output = false;

			return;

		}

		if ( !counting )
			return;

		if ( !gate && mode != 1 && mode != 5 )
			return;

		switch ( mode ) {

			case 0:
			case 1:
				count = count ? count - 1 : wrap - 1;

				if ( count == 0 )
					output = true;

				return;

			case 2:
				count = count ? count - 1 : wrap - 1;

				if ( count == 1 )
					output = false;
				else if ( count == 0 ) {

					output = true;
					count = reload;

				}

				return;

			case 3:

				if ( odd_wait ) {

					odd_wait = false;
					output = !output;
					count = reload & ~1u;

					return;

				}

				count = count > 2 ? count - 2 : 0;

				if ( count == 0 ) {

					if ( ( reload & 1 ) && output )
						odd_wait = true;
					else {

						output = !output;
						count = reload & ~1u;

					}

				}

				return;

			default:

				if ( !output ) {

					output = true;
					count = count ? count - 1 : wrap - 1;

					return;

				}

				count = count ? count - 1 : wrap - 1;

				if ( count == 0 )
					output = false;

				return;

		}

	}

	void pit::reset ( ) {

		for ( pit_channel& channel : channels ) {

			bool gate = channel.gate;

			channel = pit_channel ( );
			channel.gate = gate;

		}

	}

	void pit::clock ( ) {

		channels [ 0 ].clock ( );
		channels [ 1 ].clock ( );
		channels [ 2 ].clock ( );

	}

	void pit::set_gate ( int32_t index, bool high ) {

		pit_channel& channel = channels [ index ];
		bool rising = high && !channel.gate;

		channel.gate = high;

		if ( !high && ( channel.mode == 2 || channel.mode == 3 ) )
			channel.output = true;

		if ( !rising || !channel.armed )
			return;

		if ( channel.mode == 1 || channel.mode == 5 )
			channel.trigger_pending = true;
		else if ( channel.mode == 2 || channel.mode == 3 )
			channel.load_pending = true;

	}

	uint8_t pit::in ( uint16_t port ) {

		if ( ( port & 3 ) == 3 )
			return 0xFF;

		pit_channel& channel = channels [ port & 3 ];
		uint16_t value = channel.latched ? channel.latch : channel.visible ( );
		bool high = false;

		switch ( channel.access ) {

			case 1:
				high = false;
				channel.latched = false;
				break;

			case 2:
				high = true;
				channel.latched = false;
				break;

			default:

				if ( channel.latched ) {

					high = channel.latch_high;
					channel.latch_high = !channel.latch_high;

					if ( high )
						channel.latched = false;

				} else {

					high = channel.read_high;
					channel.read_high = !channel.read_high;

				}

				break;

		}

		return static_cast < uint8_t > ( high ? value >> 8 : value );

	}

	void pit::out ( uint16_t port, uint8_t value ) {

		if ( ( port & 3 ) == 3 ) {

			uint8_t index = static_cast < uint8_t > ( value >> 6 );

			if ( index == 3 )
				return;

			pit_channel& channel = channels [ index ];
			uint8_t access = static_cast < uint8_t > ( ( value >> 4 ) & 3 );

			if ( access == 0 ) {

				if ( !channel.latched ) {

					channel.latch = channel.visible ( );
					channel.latched = true;
					channel.latch_high = false;

				}

				return;

			}

			channel.access = access;
			channel.mode = static_cast < uint8_t > ( ( value >> 1 ) & 7 );

			if ( channel.mode > 5 )
				channel.mode = static_cast < uint8_t > ( channel.mode - 4 );

			channel.bcd = ( value & 1 ) != 0;
			channel.output = channel.mode != 0;
			channel.counting = false;
			channel.load_pending = false;
			channel.trigger_pending = false;
			channel.odd_wait = false;
			channel.latched = false;
			channel.write_high = false;
			channel.read_high = false;
			channel.armed = false;

			return;

		}

		pit_channel& channel = channels [ port & 3 ];

		switch ( channel.access ) {

			case 1:
				channel.written = value;
				break;

			case 2:
				channel.written = static_cast < uint16_t > ( value << 8 );
				break;

			default:

				if ( !channel.write_high ) {

					channel.written = static_cast < uint16_t > ( ( channel.written & 0xFF00 ) | value );
					channel.write_high = true;

					if ( channel.mode == 0 )
						channel.counting = false;

					return;

				}

				channel.written = static_cast < uint16_t > ( ( channel.written & 0x00FF ) | value << 8 );
				channel.write_high = false;
				break;

		}

		if ( channel.written == 0 )
			channel.reload = channel.bcd ? 10000 : 0x10000;
		else
			channel.reload = channel.bcd ? from_bcd ( channel.written ) : channel.written;

		channel.armed = true;

		switch ( channel.mode ) {

			case 0:
				channel.output = false;
				channel.load_pending = true;
				break;

			case 1:
			case 5:
				break;

			case 2:
			case 3:

				if ( !channel.counting )
					channel.load_pending = true;

				break;

			default:
				channel.load_pending = true;
				break;

		}

	}

}