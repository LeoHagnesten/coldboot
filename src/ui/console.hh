#pragma once

#include "ui/theme.hh"

namespace ui {

	enum console_action : uint8_t {

		TOOL_NONE,
		TOOL_START,
		TOOL_SHUT_DOWN,
		TOOL_TURN_OFF,
		TOOL_RESET,
		TOOL_KEYS,
		TOOL_DISC,
		TOOL_EJECT,
		TOOL_SHOT,
		TOOL_FULLSCREEN,
		TOOL_SCREEN,
		TOOL_FILES,
		TOOL_TYPE,
		TOOL_SAVE,
		TOOL_TOOLS,
		TOOL_COUNT,

	};

	struct console_view {

		bool light = false;
		bool running = false;
		bool failed = false;
		bool captured = false;
		bool integrated = false;
		bool fullscreen = false;
		const uint32_t* pixels = nullptr;
		int32_t frame_width = 0;
		int32_t frame_height = 0;
		const wchar_t* problem = L"";
		const wchar_t* disc = L"";
		const wchar_t* disk = L"";
		uint32_t processors = 1;
		uint32_t memory_mb = 0;
		bool disk_light = false;
		bool disc_light = false;
		bool can_save = false;
		bool tools_linked = false;

	};

	class console {

	public:

		void layout ( float view_width, float view_height, bool fullscreen );

		void paint ( render::gpu& gpu, const console_view& state, double now );

		void mouse_move ( float x, float y );

		void mouse_leave ( );

		void mouse_down ( float x, float y );

		console_action mouse_up ( float x, float y );

		bool animating ( double now ) const;

		void toast ( const wchar_t* text, double now );

		bool over_screen ( float x, float y ) const {

			return frame_area.w > 0.0f && frame_area.contains ( x, y );

		}

		render::rect frame_area;

	private:

		struct hit {

			render::rect area;
			console_action action;

		};

		void add_hit ( render::rect area, console_action action );

		console_action find ( float x, float y ) const;

		float tool ( render::gpu& gpu, const theme& colors, float x, const wchar_t* label, render::icon_kind icon, console_action action, bool enabled, bool accent, bool compact );

		void paint_toolbar ( render::gpu& gpu, const theme& colors, const console_view& state );

		void paint_screen ( render::gpu& gpu, const theme& colors, const console_view& state );

		void paint_status ( render::gpu& gpu, const theme& colors, const console_view& state, double now );

		hit hits [ 32 ];
		int32_t hit_count = 0;
		float width = 0.0f;
		float height = 0.0f;
		float toolbar = 44.0f;
		float status = 26.0f;
		bool full = false;
		console_action hovered = TOOL_NONE;
		console_action pressed = TOOL_NONE;
		wchar_t toast_text [ 96 ] = { };
		double toast_since = -100.0;

	};

}