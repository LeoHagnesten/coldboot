#pragma once

#include "base/base.hh"
#include <windows.h>

namespace vm {

	static constexpr int32_t config_version = 1;
	static constexpr int32_t name_room = 64;
	static constexpr int32_t notes_room = 512;

	enum machine_kind : uint8_t {

		MACHINE_XT,
		MACHINE_PC,

	};

	struct preset {

		const char* id;
		const wchar_t* label;
		const wchar_t* detail;
		const wchar_t* mark;
		machine_kind machine;
		uint32_t memory;
		uint32_t processors;
		uint32_t disk_gb;

	};

	const preset* presets ( int32_t& count );

	const preset* find_preset ( const char* id );

	const preset* detect_image ( const wchar_t* path );

	struct host_info {

		uint32_t memory_mb = 0;
		uint32_t logical = 1;
		uint32_t physical = 1;

	};

	host_info read_host ( );

	bool create_disk ( const wchar_t* path, uint64_t bytes );

	struct config {

		wchar_t name [ name_room ] = { };
		char os [ 24 ] = "pcxt";
		machine_kind machine = MACHINE_XT;
		uint32_t memory = 640;
		uint32_t memory_mb = 2048;
		uint32_t processors = 1;
		uint32_t cap = 100;
		bool disk_first = false;
		bool uefi = false;
		bool network = true;
		uint8_t clipboard = 3;
		uint8_t file_sharing = 3;
		bool unattended = false;
		uint16_t screen_width = 0;
		uint16_t screen_height = 0;
		uint16_t frame_rate = 0;
		uint8_t scaling = 0;
		bool smooth = false;
		bool frame_counter = false;
		bool local_network = true;
		char proxy [ 128 ] = { };
		bool use_proxy = false;
		bool ask_internet = false;
		bool fresh_identity = false;
		bool disposable = false;
		bool keep_preview = true;
		wchar_t iso [ MAX_PATH ] = { };
		wchar_t disk [ MAX_PATH ] = { };
		bool turbo = false;
		bool crt = false;
		bool fast_floppy = false;
		bool custom_bios = false;
		bool maximized = false;
		int32_t window_x = 0;
		int32_t window_y = 0;
		int32_t window_width = 0;
		int32_t window_height = 0;
		wchar_t bios [ MAX_PATH ] = { };
		wchar_t floppies [ 2 ] [ MAX_PATH ] = { };
		wchar_t notes [ notes_room ] = { };

	};

	struct screen_size {

		uint16_t width;
		uint16_t height;

	};

	static constexpr int32_t screen_choice_count = 8;

	const screen_size* screen_choices ( );

	static constexpr int32_t frame_choice_count = 6;

	const uint16_t* frame_choices ( );

	bool valid_proxy ( const char* text, int32_t length );

	struct launch_choice {

		int8_t disposable = -1;
		int8_t fresh_identity = -1;
		int8_t local_network = -1;
		int8_t use_proxy = -1;
		int8_t ask_internet = -1;

	};

	bool read_launch ( const wchar_t* const* arguments, int32_t count, launch_choice& out );

	void apply_launch ( const launch_choice& choice, config& run );

	bool private_run ( const config& run );

	bool valid_name ( const wchar_t* name );

	bool parse ( const char* text, int32_t length, config& out, char* error, int32_t room );

	void serialize ( const config& in, base::list < char >& out );

	bool load ( const wchar_t* path, config& out, char* error, int32_t room );

	bool save ( const wchar_t* path, const config& in );

}