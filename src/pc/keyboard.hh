#pragma once

#include "hv/machine.hh"
#include "pc/common.hh"

namespace pc {

	struct byte_queue {

		uint8_t data [ 64 ] = { };
		uint32_t head = 0;
		uint32_t count = 0;

		bool push ( uint8_t value ) {

			if ( count >= 64 )
				return false;

			data [ ( head + count++ ) & 63 ] = value;

			return true;

		}

		uint8_t pop ( ) {

			uint8_t value = data [ head ];

			head = ( head + 1 ) & 63;
			count--;

			return value;

		}

		void clear ( ) {

			head = 0;
			count = 0;

		}

		uint32_t room ( ) const {

			return 64 - count;

		}

	};

	class keyboard_controller : public hv::device {

	public:

		explicit keyboard_controller ( platform_bus& owner ) : bus ( owner ) { }

		void reset ( );

		void key ( uint8_t scan );

		void mouse ( int32_t dx, int32_t dy, int32_t wheel, uint8_t buttons );

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		uint8_t config = 0x47;
		uint8_t leds = 0;
		uint8_t scancode_set = 2;
		bool scanning = true;
		bool streaming = false;
		uint8_t mouse_id = 0;

	private:

		void respond ( uint8_t value );

		void keyboard_byte ( uint8_t value );

		void keyboard_command ( uint8_t value );

		void mouse_command ( uint8_t value );

		void pump ( );

		platform_bus& bus;
		byte_queue controller;
		byte_queue keys;
		byte_queue pointer;
		uint8_t output = 0;
		bool full = false;
		bool from_mouse = false;
		uint8_t waiting = 0;
		uint8_t key_waiting = 0;
		uint8_t mouse_waiting = 0;
		uint8_t last_key_reply = 0xFA;
		uint8_t rates [ 3 ] = { };
		uint8_t sample_rate = 100;
		uint8_t resolution = 2;
		uint8_t last_buttons = 0;
		bool command_last = false;

	};

}