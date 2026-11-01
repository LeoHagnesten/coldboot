#pragma once

#include "app/audio.hh"
#include "app/settings.hh"
#include "machine/xt.hh"
#include <windows.h>

namespace app {

	enum session_state : uint8_t {

		SESSION_OFF,
		SESSION_RUNNING,
		SESSION_FAILED,

	};

	struct drive_info {

		wchar_t name [ MAX_PATH ] = { };
		uint32_t kilobytes = 0;
		bool present = false;
		bool protect = false;
		bool light = false;
		bool program = false;

	};

	class session {

	public:

		static constexpr UINT frame_message = WM_APP + 1;
		static constexpr UINT light_message = WM_APP + 2;
		static constexpr int32_t drive_count = 2;

		session ( );

		~session ( );

		bool power_on ( HWND window, const settings& config );

		void power_off ( );

		void reset ( );

		void set_paused ( bool value );

		void set_turbo ( bool value );

		void set_fast_floppy ( bool value );

		bool insert ( int32_t drive, const wchar_t* path );

		void eject ( int32_t drive );

		void set_protect ( int32_t drive, bool value );

		bool save_state ( const wchar_t* path );

		bool load_state ( const wchar_t* path );

		void key ( uint8_t scan );

		bool latest ( uint32_t* pixels, int32_t& width, int32_t& height );

		void drive ( int32_t index, drive_info& out );

		const wchar_t* disk_path ( int32_t index ) const {

			return disks [ index ];

		}

		double speed ( ) const {

			return measured;

		}

		uint8_t post_code ( ) const {

			return code;

		}

		session_state state = SESSION_OFF;
		bool paused = false;
		bool turbo = false;
		bool muted = false;
		wchar_t problem [ 200 ] = { };
		wchar_t note [ 200 ] = { };

	private:

		static DWORD __stdcall loop ( void* self );

		void run ( );

		void lock_board ( );

		void unlock_board ( );

		void save_disk ( int32_t index );

		machine::xt* board = nullptr;
		audio sound;
		HWND window_handle = nullptr;
		HANDLE thread = nullptr;
		volatile long quit = 0;
		volatile long posted = 0;
		volatile long lights = 0;
		volatile long requests = 0;
		volatile long resync = 0;
		SRWLOCK board_lock = SRWLOCK_INIT;
		SRWLOCK frame_lock = SRWLOCK_INIT;
		SRWLOCK key_lock = SRWLOCK_INIT;
		uint8_t keys [ 128 ] = { };
		uint32_t key_count = 0;
		uint32_t* shown = nullptr;
		int32_t shown_width = 0;
		int32_t shown_height = 0;
		double measured = 0.0;
		uint8_t code = 0;
		wchar_t disks [ drive_count ] [ MAX_PATH ] = { };

	};

}