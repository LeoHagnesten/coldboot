#pragma once

#include "pc/common.hh"

namespace pc {

	static constexpr int32_t usb_nak = -1;
	static constexpr int32_t usb_stall = -2;
	static constexpr int32_t usb_timeout = -3;
	static constexpr uint8_t pid_setup = 0x2D;
	static constexpr uint8_t pid_in = 0x69;
	static constexpr uint8_t pid_out = 0xE1;

	class usb_device {

	public:

		virtual ~usb_device ( ) = default;

		void reset ( );

		int32_t transfer ( uint8_t pid, uint8_t endpoint, uint8_t* data, int32_t length );

		uint8_t address = 0;
		uint8_t configuration = 0;

	protected:

		virtual int32_t descriptor ( uint8_t type, uint8_t index, uint8_t* reply, int32_t room ) = 0;

		virtual int32_t request ( const uint8_t*, uint8_t*, int32_t ) {

			return usb_stall;

		}

		virtual int32_t poll ( uint8_t, uint8_t*, int32_t ) {

			return usb_stall;

		}

		virtual void restart ( ) { }

		static int32_t text ( const char* value, uint8_t* reply, int32_t room );

		static int32_t give ( const uint8_t* data, int32_t length, uint8_t* reply, int32_t room );

	private:

		enum control_stage : uint8_t {

			STAGE_IDLE,
			STAGE_DATA_IN,
			STAGE_DATA_OUT,
			STAGE_STATUS_IN,
			STAGE_STALLED,

		};

		int32_t handle ( const uint8_t* setup, uint8_t* buffer, int32_t room );

		uint8_t setup [ 8 ] = { };
		uint8_t buffer [ 1024 ] = { };
		int32_t length = 0;
		int32_t cursor = 0;
		int32_t wanted = 0;
		int32_t next_address = -1;
		control_stage stage = STAGE_IDLE;
		bool wakeup = false;

	};

	class usb_tablet : public usb_device {

	public:

		static constexpr int32_t span = 32768;

		void move ( int32_t x, int32_t y, int32_t wheel, uint8_t buttons );

		volatile long polls = 0;

	protected:

		int32_t descriptor ( uint8_t type, uint8_t index, uint8_t* reply, int32_t room ) override;

		int32_t request ( const uint8_t* setup, uint8_t* reply, int32_t room ) override;

		int32_t poll ( uint8_t endpoint, uint8_t* data, int32_t room ) override;

		void restart ( ) override;

	private:

		int32_t report ( uint8_t* out );

		int32_t x = span / 2;
		int32_t y = span / 2;
		int32_t scroll = 0;
		uint8_t pressed = 0;
		uint8_t idle = 0;
		uint8_t protocol = 1;
		bool changed = false;

	};

}