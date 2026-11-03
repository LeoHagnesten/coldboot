#pragma once

#include "net/nat.hh"
#include "pc/bios.hh"
#include "pc/e1000.hh"
#include "pc/hda.hh"
#include "pc/platform.hh"
#include "pc/tools.hh"
#include "pc/uefi.hh"
#include "pc/uhci.hh"

namespace pc {

	struct computer_options {

		uint32_t processors = 1;
		uint64_t memory = 2048ull * 1024 * 1024;
		uint32_t cap = 100;
		bool optical_first = true;
		bool network = true;
		bool uefi = false;
		uint32_t sharing = SHARE_TEXT_IN | SHARE_TEXT_OUT | SHARE_FILES_IN | SHARE_FILES_OUT;
		uint16_t screen_width = 0;
		uint16_t screen_height = 0;
		bool local_network = true;
		bool ask_internet = false;
		bool fresh_identity = false;
		bool disposable = false;
		char proxy [ 128 ] = { };
		wchar_t iso [ MAX_PATH ] = { };
		wchar_t disk [ MAX_PATH ] = { };
		wchar_t helper [ MAX_PATH ] = { };
		wchar_t variables [ MAX_PATH ] = { };

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

		void pointer ( int32_t x, int32_t y, int32_t wheel, uint8_t buttons );

		bool pointer_ready ( );

		void press_power ( );

		bool change_disc ( const wchar_t* path );

		bool change_second_disc ( const wchar_t* path );

		bool screen ( uint32_t* out, int32_t& width, int32_t& height, double now );

		uint64_t screen_changes ( ) const {

			return video ? video->changes : 0;

		}

		int32_t console_text ( char* out, int32_t room );

		bool running ( ) const {

			return board && board->vm.running ( );

		}

		computer_options options;
		char problem [ 256 ] = { };
		platform* board = nullptr;
		vga* video = nullptr;
		ide_controller* storage = nullptr;
		uhci* usb = nullptr;
		usb_tablet* tablet = nullptr;
		e1000* nic = nullptr;
		net::nat* router = nullptr;
		hda* sound = nullptr;
		sound_sink* speaker = nullptr;
		bios* firmware = nullptr;
		uefi* uefi_firmware = nullptr;
		disk_file disc;
		disk_file drive;
		disk_file helper;
		guest_tools tools;

	private:

		bool fail ( const char* text );

		void release ( );

		long seen_polls = 0;
		uint64_t polled_at = 0;
		uint64_t identity_seed = 0;

	};

}