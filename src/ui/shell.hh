#pragma once

#include "ui/theme.hh"

namespace ui {

	enum control : uint16_t {

		CONTROL_NONE,
		CONTROL_POWER,
		CONTROL_RESET,
		CONTROL_PAUSE,
		CONTROL_SAVE,
		CONTROL_LOAD,
		CONTROL_SHOT,
		CONTROL_SPEED,
		CONTROL_DISKS,
		CONTROL_SETTINGS,
		CONTROL_FULLSCREEN,
		CONTROL_SCREEN,
		CONTROL_START,
		CONTROL_DRIVE_A,
		CONTROL_DRIVE_B,
		CONTROL_INSERT_A,
		CONTROL_INSERT_B,
		CONTROL_EJECT_A,
		CONTROL_EJECT_B,
		CONTROL_PROTECT_A,
		CONTROL_PROTECT_B,
		CONTROL_NEW_360,
		CONTROL_NEW_720,
		CONTROL_NEW_1200,
		CONTROL_NEW_1440,
		CONTROL_SPEED_REAL,
		CONTROL_SPEED_TURBO,
		CONTROL_DISPLAY_SHARP,
		CONTROL_DISPLAY_CRT,
		CONTROL_THEME_DARK,
		CONTROL_THEME_LIGHT,
		CONTROL_FLOPPY_REAL,
		CONTROL_FLOPPY_FAST,
		CONTROL_MEMORY_256,
		CONTROL_MEMORY_512,
		CONTROL_MEMORY_640,
		CONTROL_BIOS_BUILTIN,
		CONTROL_BIOS_CUSTOM,
		CONTROL_COUNT,

	};

	enum popover_kind : uint8_t {

		POPOVER_NONE,
		POPOVER_DISKS,
		POPOVER_SETTINGS,

	};

	struct drive_view {

		const wchar_t* name = L"";
		uint32_t kilobytes = 0;
		bool present = false;
		bool protect = false;
		bool light = false;
		bool program = false;

	};

	struct view {

		const uint32_t* pixels = nullptr;
		int32_t frame_width = 0;
		int32_t frame_height = 0;
		bool powered = false;
		bool failed = false;
		bool paused = false;
		bool turbo = false;
		bool crt = false;
		bool light = false;
		bool fast_floppy = false;
		bool custom_bios = false;
		bool captured = false;
		bool fullscreen = false;
		bool can_load = false;
		uint8_t post_code = 0;
		double speed = 0.0;
		uint32_t memory = 640;
		drive_view drives [ 2 ];
		const wchar_t* bios_name = L"";
		const wchar_t* problem = L"";

	};

	struct hit {

		render::rect area;
		control id;

	};

	class shell {

	public:

		void layout ( float view_width, float view_height, bool fullscreen );

		void paint ( render::gpu& gpu, const view& state, double now );

		void mouse_move ( float x, float y, double now );

		void mouse_leave ( );

		void mouse_down ( float x, float y );

		control mouse_up ( float x, float y );

		bool animating ( double now ) const;

		void toast ( const wchar_t* text, double now );

		void open_popover ( popover_kind kind, double now );

		render::rect anchor_of ( control id ) const {

			return anchors [ id ];

		}

		bool over_screen ( float x, float y ) const {

			return screen_area.contains ( x, y ) && popover == POPOVER_NONE;

		}

		render::rect screen_area;
		popover_kind popover = POPOVER_NONE;

	private:

		popover_kind shown = POPOVER_NONE;

		void add_hit ( render::rect area, control id );

		control find ( float x, float y ) const;

		float glow ( control id ) const {

			return glows [ id ];

		}

		bool button ( render::gpu& gpu, const theme& colors, render::rect area, control id, render::icon_kind icon, bool active, bool enabled, render::color tint );

		void segmented ( render::gpu& gpu, const theme& colors, render::rect area, const control* ids, const wchar_t* const* labels, int32_t count, int32_t selected );

		void paint_toolbar ( render::gpu& gpu, const theme& colors, const view& state );

		void paint_status ( render::gpu& gpu, const theme& colors, const view& state );

		void paint_screen ( render::gpu& gpu, const theme& colors, const view& state, double now );

		void paint_disks ( render::gpu& gpu, const theme& colors, const view& state, render::rect panel );

		void paint_settings ( render::gpu& gpu, const theme& colors, const view& state, render::rect panel );

		void paint_tooltip ( render::gpu& gpu, const theme& colors, double now );

		render::rect anchor_panel ( control anchor, float panel_width, float panel_height ) const;

		hit hits [ 96 ];
		int32_t hit_count = 0;
		render::rect anchors [ CONTROL_COUNT ];
		float glows [ CONTROL_COUNT ] = { };
		float width = 0.0f;
		float height = 0.0f;
		bool full = false;
		float toolbar = 52.0f;
		float status = 32.0f;
		control hovered = CONTROL_NONE;
		control pressed = CONTROL_NONE;
		double hover_since = 0.0;
		double last_paint = 0.0;
		double popover_since = 0.0;
		float popover_amount = 0.0f;
		render::rect popover_area;
		wchar_t toast_text [ 96 ] = { };
		double toast_since = -100.0;
		float mouse_x = -1.0f;
		float mouse_y = -1.0f;

	};

}