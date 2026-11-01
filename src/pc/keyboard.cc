#include "pc/keyboard.hh"

namespace pc {

	static const uint8_t set_two [ 0x5E ] = {

		0x00, 0x76, 0x16, 0x1E, 0x26, 0x25, 0x2E, 0x36, 0x3D, 0x3E, 0x46, 0x45, 0x4E, 0x55, 0x66, 0x0D,
		0x15, 0x1D, 0x24, 0x2D, 0x2C, 0x35, 0x3C, 0x43, 0x44, 0x4D, 0x54, 0x5B, 0x5A, 0x14, 0x1C, 0x1B,
		0x23, 0x2B, 0x34, 0x33, 0x3B, 0x42, 0x4B, 0x4C, 0x52, 0x0E, 0x12, 0x5D, 0x1A, 0x22, 0x21, 0x2A,
		0x32, 0x31, 0x3A, 0x41, 0x49, 0x4A, 0x59, 0x7C, 0x11, 0x29, 0x58, 0x05, 0x06, 0x04, 0x0C, 0x03,
		0x0B, 0x83, 0x0A, 0x01, 0x09, 0x77, 0x7E, 0x6C, 0x75, 0x7D, 0x7B, 0x6B, 0x73, 0x74, 0x79, 0x69,
		0x72, 0x7A, 0x70, 0x71, 0x84, 0x00, 0x61, 0x78, 0x07, 0x00, 0x00, 0x1F, 0x27, 0x2F,

	};

	void keyboard_controller::reset ( ) {

		controller.clear ( );
		keys.clear ( );
		pointer.clear ( );
		config = 0x47;
		leds = 0;
		scancode_set = 2;
		scanning = true;
		streaming = false;
		mouse_id = 0;
		output = 0;
		full = false;
		from_mouse = false;
		waiting = 0;
		key_waiting = 0;
		mouse_waiting = 0;
		sample_rate = 100;
		resolution = 2;
		command_last = false;

	}

	void keyboard_controller::pump ( ) {

		if ( full )
			return;

		if ( controller.count ) {

			output = controller.pop ( );
			from_mouse = false;

		} else if ( keys.count && !( config & 0x10 ) ) {

			output = keys.pop ( );
			from_mouse = false;

		} else if ( pointer.count && !( config & 0x20 ) ) {

			output = pointer.pop ( );
			from_mouse = true;

		} else
			return;

		full = true;

		if ( from_mouse ? ( config & 0x02 ) : ( config & 0x01 ) )
			bus.set_irq ( from_mouse ? 12 : 1, true );

	}

	void keyboard_controller::respond ( uint8_t value ) {

		controller.push ( value );
		pump ( );

	}

	void keyboard_controller::keyboard_byte ( uint8_t value ) {

		last_key_reply = value;
		keys.push ( value );

	}

	void keyboard_controller::key ( uint8_t scan ) {

		if ( !scanning )
			return;

		if ( ( config & 0x40 ) || scancode_set == 1 || scan == 0xE0 ) {

			keys.push ( scan );
			pump ( );

			return;

		}

		uint8_t make = static_cast < uint8_t > ( scan & 0x7F );
		uint8_t code = make < sizeof ( set_two ) ? set_two [ make ] : 0;

		if ( !code )
			return;

		if ( ( scan & 0x80 ) && keys.room ( ) >= 2 )
			keys.push ( 0xF0 );

		keys.push ( code );
		pump ( );

	}

	void keyboard_controller::mouse ( int32_t dx, int32_t dy, int32_t wheel, uint8_t buttons ) {

		uint32_t size = mouse_id == 3 ? 4u : 3u;

		if ( !streaming || pointer.room ( ) < size )
			return;

		int32_t across = base::clamp ( dx, -255, 255 );
		int32_t up = base::clamp ( -dy, -255, 255 );

		pointer.push ( static_cast < uint8_t > ( ( buttons & 7 ) | 0x08 | ( across < 0 ? 0x10 : 0 ) | ( up < 0 ? 0x20 : 0 ) ) );
		pointer.push ( static_cast < uint8_t > ( across ) );
		pointer.push ( static_cast < uint8_t > ( up ) );

		if ( size == 4 )
			pointer.push ( static_cast < uint8_t > ( base::clamp ( -wheel, -8, 7 ) & 0x0F ) );

		last_buttons = buttons;
		pump ( );

	}

	void keyboard_controller::keyboard_command ( uint8_t value ) {

		if ( key_waiting ) {

			uint8_t command = key_waiting;

			key_waiting = 0;
			keyboard_byte ( 0xFA );

			if ( command == 0xED )
				leds = static_cast < uint8_t > ( value & 7 );
			else if ( command == 0xF0 ) {

				static const uint8_t translated [ 3 ] = { 0x43, 0x41, 0x3F };

				if ( !value )
					keyboard_byte ( config & 0x40 ? translated [ scancode_set - 1 ] : scancode_set );
				else if ( value <= 3 )
					scancode_set = value;

			}

			pump ( );

			return;

		}

		switch ( value ) {

			case 0xED:
			case 0xF3:
			case 0xF0:
				key_waiting = value;
				keyboard_byte ( 0xFA );
				break;

			case 0xEE:
				keyboard_byte ( 0xEE );
				break;

			case 0xF2:
				keyboard_byte ( 0xFA );
				keyboard_byte ( 0xAB );
				keyboard_byte ( config & 0x40 ? 0x41 : 0x83 );
				break;

			case 0xF4:
			case 0xF6:
				scanning = true;
				keyboard_byte ( 0xFA );
				break;

			case 0xF5:
				scanning = false;
				keyboard_byte ( 0xFA );
				break;

			case 0xFE:
				keys.push ( last_key_reply );
				break;

			case 0xFF:
				keys.clear ( );
				scanning = true;
				scancode_set = 2;
				keyboard_byte ( 0xFA );
				keyboard_byte ( 0xAA );
				break;

			default:
				keyboard_byte ( 0xFA );
				break;

		}

		pump ( );

	}

	void keyboard_controller::mouse_command ( uint8_t value ) {

		if ( mouse_waiting ) {

			if ( mouse_waiting == 0xF3 ) {

				sample_rate = value;
				rates [ 0 ] = rates [ 1 ];
				rates [ 1 ] = rates [ 2 ];
				rates [ 2 ] = value;

				if ( rates [ 0 ] == 200 && rates [ 1 ] == 100 && rates [ 2 ] == 80 )
					mouse_id = 3;

			} else
				resolution = value;

			mouse_waiting = 0;
			pointer.push ( 0xFA );
			pump ( );

			return;

		}

		switch ( value ) {

			case 0xFF:
				pointer.clear ( );
				streaming = false;
				mouse_id = 0;
				sample_rate = 100;
				resolution = 2;
				pointer.push ( 0xFA );
				pointer.push ( 0xAA );
				pointer.push ( 0x00 );
				break;

			case 0xF6:
				streaming = false;
				sample_rate = 100;
				resolution = 2;
				pointer.push ( 0xFA );
				break;

			case 0xF5:
				streaming = false;
				pointer.push ( 0xFA );
				break;

			case 0xF4:
				streaming = true;
				pointer.push ( 0xFA );
				break;

			case 0xF3:
			case 0xE8:
				mouse_waiting = value;
				pointer.push ( 0xFA );
				break;

			case 0xF2:
				pointer.push ( 0xFA );
				pointer.push ( mouse_id );
				break;

			case 0xE9:
				pointer.push ( 0xFA );
				pointer.push ( static_cast < uint8_t > ( ( streaming ? 0x20 : 0 ) | ( last_buttons & 7 ) ) );
				pointer.push ( resolution );
				pointer.push ( sample_rate );
				break;

			case 0xEB:
				pointer.push ( 0xFA );
				pointer.push ( 0x08 );
				pointer.push ( 0x00 );
				pointer.push ( 0x00 );
				break;

			default:
				pointer.push ( 0xFA );
				break;

		}

		pump ( );

	}

	uint64_t keyboard_controller::read ( uint64_t port, uint32_t ) {

		if ( port == 0x64 )
			return ( full ? 0x01 : 0 ) | ( config & 0x04 ) | ( command_last ? 0x08 : 0 ) | 0x10 | ( full && from_mouse ? 0x20 : 0 );

		uint8_t value = output;

		if ( full ) {

			full = false;
			bus.set_irq ( from_mouse ? 12 : 1, false );
			pump ( );

		}

		return value;

	}

	void keyboard_controller::write ( uint64_t port, uint32_t, uint64_t raw ) {

		uint8_t value = static_cast < uint8_t > ( raw );

		if ( port == 0x64 ) {

			command_last = true;

			if ( value == 0x20 )
				respond ( config );
			else if ( value > 0x20 && value < 0x40 )
				respond ( 0 );
			else if ( ( value >= 0x60 && value < 0x80 ) || value == 0xD1 || value == 0xD2 || value == 0xD3 || value == 0xD4 )
				waiting = value;
			else if ( value == 0xA7 )
				config |= 0x20;
			else if ( value == 0xA8 ) {

				config &= ~0x20;
				pump ( );

			} else if ( value == 0xA9 || value == 0xAB || value == 0xE0 )
				respond ( 0x00 );
			else if ( value == 0xAA )
				respond ( 0x55 );
			else if ( value == 0xAD )
				config |= 0x10;
			else if ( value == 0xAE ) {

				config &= ~0x10;
				pump ( );

			} else if ( value == 0xC0 )
				respond ( 0xBF );
			else if ( value == 0xD0 )
				respond ( static_cast < uint8_t > ( 0x03 | ( full ? 0x10 : 0 ) | ( full && from_mouse ? 0x20 : 0 ) ) );
			else if ( value >= 0xF0 && !( value & 1 ) )
				bus.request_reset ( );

			return;

		}

		command_last = false;

		if ( !waiting ) {

			keyboard_command ( value );

			return;

		}

		uint8_t command = waiting;

		waiting = 0;

		if ( command == 0x60 ) {

			config = value;
			pump ( );

		} else if ( command == 0xD1 ) {

			if ( !( value & 1 ) )
				bus.request_reset ( );

		} else if ( command == 0xD2 ) {

			keys.push ( value );
			pump ( );

		} else if ( command == 0xD3 ) {

			pointer.push ( value );
			pump ( );

		} else if ( command == 0xD4 )
			mouse_command ( value );

	}

}