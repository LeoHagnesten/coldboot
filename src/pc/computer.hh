#pragma once

#include "pc/bios.hh"
#include "pc/platform.hh"

namespace pc {

	struct computer_options {

		uint32_t processors = 1;
		uint64_t memory = 2048ull * 1024 * 1024;
		uint32_t cap = 100;
		bool optical_first = true;
		wchar_t iso [ MAX_PATH ] = { };
		wchar_t disk [ MAX_PATH ] = { };
		wchar_t helper [ MAX_PATH ] = { };

	};

	bool make_setup_helper ( const wchar_t* folder, wchar_t* out, char* error, int32_t room );

	enum computer_event : uint8_t {

		EVENT_NONE,
		EVENT_RESET,
		EVENT_OFF,
		EVENT_FAILED,

	};

	class computer {

	public:

		computer ( ) = default;

		computer ( const computer& ) = delete;

		computer& operator= ( const computer& ) = delete;

		~computer ( );

		bool power_on ( const computer_options& wanted );

		void power_off ( );

		bool restart ( );

		computer_event poll ( );

		void key ( uint8_t scan, bool extended );

		void mouse ( int32_t dx, int32_t dy, int32_t wheel, uint8_t buttons );

		void press_power ( );

		bool change_disc ( const wchar_t* path );

		bool screen ( uint32_t* out, int32_t& width, int32_t& height, double now );

		int32_t console_text ( char* out, int32_t room );

		bool running ( ) const {

			return board && board->vm.running ( );

		}

		computer_options options;
		char problem [ 256 ] = { };
		platform* board = nullptr;
		vga* video = nullptr;
		ide_controller* storage = nullptr;
		bios* firmware = nullptr;
		disk_file disc;
		disk_file drive;
		disk_file helper;

	private:

		bool fail ( const char* text );

		void release ( );

	};

}