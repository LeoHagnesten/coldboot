#include "pc/usb.hh"
#include <windows.h>

namespace pc {

	static const uint8_t tablet_report [ ] = {

		0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
		0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
		0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
		0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x7F, 0x35, 0x00, 0x46, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
		0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x35, 0x00, 0x45, 0x00, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
		0xC0, 0xC0,

	};

	static const uint8_t tablet_device [ 18 ] = {

		18, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 8,
		0x09, 0x12, 0x01, 0xCB, 0x00, 0x01, 1, 2, 3, 1,

	};

	static const uint8_t tablet_configuration [ 34 ] = {

		9, 0x02, 34, 0, 1, 1, 0, 0xA0, 50,
		9, 0x04, 0, 0, 1, 0x03, 0x00, 0x00, 0,
		9, 0x21, 0x11, 0x01, 0x00, 1, 0x22, sizeof ( tablet_report ), 0,
		7, 0x05, 0x81, 0x03, 8, 0, 8,

	};

	void usb_device::reset ( ) {

		address = 0;
		configuration = 0;
		stage = STAGE_IDLE;
		next_address = -1;
		wakeup = false;
		restart ( );

	}

	int32_t usb_device::text ( const char* value, uint8_t* reply, int32_t room ) {

		int32_t count = 0;

		while ( value [ count ] )
			count++;

		int32_t total = 2 + count * 2;

		if ( room >= 1 )
			reply [ 0 ] = static_cast < uint8_t > ( total );

		if ( room >= 2 )
			reply [ 1 ] = 0x03;

		for ( int32_t i = 0; i < count && 2 + i * 2 + 1 < room; i++ ) {

			reply [ 2 + i * 2 ] = static_cast < uint8_t > ( value [ i ] );
			reply [ 3 + i * 2 ] = 0;

		}

		return total < room ? total : room;

	}

	int32_t usb_device::give ( const uint8_t* data, int32_t length, uint8_t* reply, int32_t room ) {

		int32_t count = length < room ? length : room;

		for ( int32_t i = 0; i < count; i++ )
			reply [ i ] = data [ i ];

		return count;

	}

	int32_t usb_device::handle ( const uint8_t* packet, uint8_t* data, int32_t room ) {

		uint8_t type = packet [ 0 ];
		uint8_t code = packet [ 1 ];
		uint16_t value = static_cast < uint16_t > ( packet [ 2 ] | packet [ 3 ] << 8 );

		if ( ( type & 0x60 ) == 0 ) {

			uint8_t recipient = type & 0x1F;

			switch ( code ) {

				case 0x00:

					if ( room < 2 )
						return usb_stall;

					data [ 0 ] = static_cast < uint8_t > ( recipient == 0 && wakeup ? 2 : 0 );
					data [ 1 ] = 0;
					return 2;

				case 0x01:
				case 0x03:

					if ( recipient == 0 && value == 1 )
						wakeup = code == 0x03;

					return 0;

				case 0x05:
					next_address = value & 0x7F;
					return 0;

				case 0x06:

					if ( recipient == 0 )
						return descriptor ( static_cast < uint8_t > ( value >> 8 ), static_cast < uint8_t > ( value ), data, room );

					break;

				case 0x08:

					if ( room < 1 )
						return usb_stall;

					data [ 0 ] = configuration;
					return 1;

				case 0x09:

					if ( ( value & 0xFF ) > 1 )
						return usb_stall;

					configuration = static_cast < uint8_t > ( value );
					return 0;

				case 0x0A:

					if ( room < 1 )
						return usb_stall;

					data [ 0 ] = 0;
					return 1;

				case 0x0B:
					return 0;

				default:
					break;

			}

		}

		return request ( packet, data, room );

	}

	int32_t usb_device::transfer ( uint8_t pid, uint8_t endpoint, uint8_t* data, int32_t size ) {

		if ( endpoint ) {

			if ( !configuration || pid != pid_in )
				return usb_stall;

			return poll ( endpoint, data, size );

		}

		if ( pid == pid_setup ) {

			if ( size != 8 )
				return usb_stall;

			for ( int32_t i = 0; i < 8; i++ )
				setup [ i ] = data [ i ];

			int32_t requested = setup [ 6 ] | setup [ 7 ] << 8;

			next_address = -1;

			if ( setup [ 0 ] & 0x80 ) {

				int32_t room = requested < static_cast < int32_t > ( sizeof ( buffer ) ) ? requested : static_cast < int32_t > ( sizeof ( buffer ) );
				int32_t got = handle ( setup, buffer, room );

				if ( got < 0 )
					stage = STAGE_STALLED;
				else {

					length = got;
					cursor = 0;
					stage = STAGE_DATA_IN;

				}

			} else if ( !requested ) {

				stage = handle ( setup, buffer, 0 ) < 0 ? STAGE_STALLED : STAGE_STATUS_IN;

			} else {

				wanted = requested < static_cast < int32_t > ( sizeof ( buffer ) ) ? requested : static_cast < int32_t > ( sizeof ( buffer ) );
				length = 0;
				stage = STAGE_DATA_OUT;

			}

			return 8;

		}

		if ( stage == STAGE_STALLED )
			return usb_stall;

		if ( pid == pid_in ) {

			if ( stage == STAGE_DATA_IN ) {

				int32_t count = length - cursor < size ? length - cursor : size;

				for ( int32_t i = 0; i < count; i++ )
					data [ i ] = buffer [ cursor + i ];

				cursor += count;

				return count;

			}

			if ( stage == STAGE_STATUS_IN ) {

				stage = STAGE_IDLE;

				if ( next_address >= 0 )
					address = static_cast < uint8_t > ( next_address );

				next_address = -1;

				return 0;

			}

			return usb_stall;

		}

		if ( pid == pid_out ) {

			if ( stage == STAGE_DATA_IN ) {

				stage = STAGE_IDLE;

				return 0;

			}

			if ( stage == STAGE_DATA_OUT ) {

				for ( int32_t i = 0; i < size && length < wanted; i++ )
					buffer [ length++ ] = data [ i ];

				if ( length >= wanted )
					stage = handle ( setup, buffer, length ) < 0 ? STAGE_STALLED : STAGE_STATUS_IN;

				return size;

			}

		}

		return usb_stall;

	}

	void usb_tablet::move ( int32_t to_x, int32_t to_y, int32_t wheel, uint8_t buttons ) {

		to_x = to_x < 0 ? 0 : ( to_x >= span ? span - 1 : to_x );
		to_y = to_y < 0 ? 0 : ( to_y >= span ? span - 1 : to_y );

		if ( to_x != x || to_y != y || wheel || buttons != pressed )
			changed = true;

		x = to_x;
		y = to_y;
		scroll += wheel;
		pressed = buttons;

	}

	int32_t usb_tablet::report ( uint8_t* out ) {

		int32_t wheel = scroll < -127 ? -127 : ( scroll > 127 ? 127 : scroll );

		out [ 0 ] = static_cast < uint8_t > ( pressed & 7 );
		out [ 1 ] = static_cast < uint8_t > ( x );
		out [ 2 ] = static_cast < uint8_t > ( x >> 8 );
		out [ 3 ] = static_cast < uint8_t > ( y );
		out [ 4 ] = static_cast < uint8_t > ( y >> 8 );
		out [ 5 ] = static_cast < uint8_t > ( static_cast < int8_t > ( wheel ) );
		scroll -= wheel;

		return 6;

	}

	int32_t usb_tablet::descriptor ( uint8_t type, uint8_t index, uint8_t* reply, int32_t room ) {

		if ( type == 0x01 )
			return give ( tablet_device, sizeof ( tablet_device ), reply, room );

		if ( type == 0x02 )
			return give ( tablet_configuration, sizeof ( tablet_configuration ), reply, room );

		if ( type == 0x03 ) {

			static const uint8_t languages [ 4 ] = { 4, 0x03, 0x09, 0x04 };

			if ( index == 0 )
				return give ( languages, 4, reply, room );

			if ( index == 1 )
				return text ( "Coldboot", reply, room );

			if ( index == 2 )
				return text ( "Coldboot USB Tablet", reply, room );

			if ( index == 3 )
				return text ( "CB-TABLET-1", reply, room );

		}

		return usb_stall;

	}

	int32_t usb_tablet::request ( const uint8_t* setup, uint8_t* reply, int32_t room ) {

		uint8_t type = setup [ 0 ];
		uint8_t code = setup [ 1 ];
		uint16_t value = static_cast < uint16_t > ( setup [ 2 ] | setup [ 3 ] << 8 );

		if ( type == 0x81 && code == 0x06 ) {

			if ( value >> 8 == 0x22 )
				return give ( tablet_report, sizeof ( tablet_report ), reply, room );

			if ( value >> 8 == 0x21 )
				return give ( tablet_configuration + 18, 9, reply, room );

			return usb_stall;

		}

		if ( type == 0xA1 ) {

			if ( code == 0x01 ) {

				uint8_t out [ 6 ];

				report ( out );

				return give ( out, 6, reply, room );

			}

			if ( code == 0x02 && room >= 1 ) {

				reply [ 0 ] = idle;

				return 1;

			}

			if ( code == 0x03 && room >= 1 ) {

				reply [ 0 ] = protocol;

				return 1;

			}

			return usb_stall;

		}

		if ( type == 0x21 ) {

			if ( code == 0x0A ) {

				idle = static_cast < uint8_t > ( value >> 8 );

				return 0;

			}

			if ( code == 0x0B ) {

				protocol = static_cast < uint8_t > ( value & 1 );

				return 0;

			}

			if ( code == 0x09 )
				return 0;

		}

		return usb_stall;

	}

	int32_t usb_tablet::poll ( uint8_t endpoint, uint8_t* data, int32_t room ) {

		if ( endpoint != 1 )
			return usb_stall;

		InterlockedIncrement ( &polls );

		if ( !changed || room < 6 )
			return usb_nak;

		int32_t sent = report ( data );

		changed = scroll != 0;

		return sent;

	}

	void usb_tablet::restart ( ) {

		idle = 0;
		protocol = 1;
		changed = false;
		scroll = 0;

	}

}