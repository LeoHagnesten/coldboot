#pragma once

#include "app/audio.hh"
#include "machine/xt.hh"
#include <windows.h>

namespace app {

	enum session_state : uint8_t {

		SESSION_EMPTY,
		SESSION_RUNNING,
		SESSION_FAILED,

	};

	struct disk_swap {

		base::list < char > bytes;
		wchar_t path [ MAX_PATH ] = { };
		bool protect = false;
		bool waiting = false;

	};

	class session {

	public:

		static constexpr UINT frame_message = WM_APP + 1;
		static constexpr UINT light_message = WM_APP + 2;
		static constexpr int32_t drive_count = 2;

		~session ( );

		bool start ( const wchar_t* bios_path, HWND window );

		void stop ( );

		bool insert ( int32_t drive, const wchar_t* path );

		void key ( uint8_t scan );

		bool latest ( uint32_t* pixels, int32_t& width, int32_t& height );

		bool disk_name ( int32_t drive, wchar_t* out, int32_t room );

		bool light ( int32_t drive ) const {

			return ( lights >> drive & 1 ) != 0;

		}

		session_state state = SESSION_EMPTY;
		wchar_t bios [ MAX_PATH ] = { };

	private:

		static DWORD __stdcall loop ( void* self );

		void run ( );

		void apply_swaps ( );

		void save_disk ( int32_t drive );

		machine::xt* board = nullptr;
		audio sound;
		HWND window_handle = nullptr;
		HANDLE thread = nullptr;
		volatile long quit = 0;
		volatile long posted = 0;
		volatile long lights = 0;
		SRWLOCK lock = SRWLOCK_INIT;
		uint8_t keys [ 64 ] = { };
		uint32_t key_count = 0;
		uint32_t* shown = nullptr;
		int32_t shown_width = 0;
		int32_t shown_height = 0;
		disk_swap swaps [ drive_count ];
		wchar_t disks [ drive_count ] [ MAX_PATH ] = { };

	};

}