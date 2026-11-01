#pragma once

#include "ui/theme.hh"

namespace ui {

	static constexpr int32_t path_room = 260;

	enum manager_action : uint16_t {

		ACTION_NONE,
		ACTION_SELECT,
		ACTION_START,
		ACTION_NEW,
		ACTION_ADD,
		ACTION_SETTINGS,
		ACTION_REMOVE,
		ACTION_OPEN_FOLDER,
		ACTION_CREATE,
		ACTION_SAVE,
		ACTION_FORGET,
		ACTION_DELETE,
		ACTION_PICK_FLOPPY,
		ACTION_PICK_BIOS,
		ACTION_CANCEL,
		ACTION_PRESET,
		ACTION_MEMORY,
		ACTION_EJECT,
		ACTION_BIOS_BUILTIN,
		ACTION_FIELD,
		ACTION_SHEET,
		ACTION_COUNT,

	};

	enum sheet_kind : uint8_t {

		SHEET_NONE,
		SHEET_NEW,
		SHEET_SETTINGS,
		SHEET_REMOVE,

	};

	struct machine_card {

		const wchar_t* name = L"";
		const wchar_t* mark = L"XT";
		const wchar_t* kind = L"IBM PC/XT";
		const wchar_t* problem = nullptr;
		uint32_t memory = 640;
		bool running = false;
		const wchar_t* floppies [ 2 ] = { L"", L"" };
		const wchar_t* bios = L"Coldboot BIOS";
		const wchar_t* folder = L"";
		const wchar_t* notes = L"";
		const uint32_t* preview = nullptr;
		int32_t preview_width = 0;
		int32_t preview_height = 0;

	};

	struct preset_card {

		const wchar_t* label = L"";
		const wchar_t* detail = L"";
		const wchar_t* mark = L"";

	};

	struct manager_view {

		bool light = false;
		const machine_card* machines = nullptr;
		int32_t count = 0;
		int32_t selected = -1;
		const preset_card* presets = nullptr;
		int32_t preset_count = 0;
		const wchar_t* location = L"";

	};

	struct text_field {

		static constexpr int32_t room = 63;

		wchar_t text [ room + 1 ] = { };
		int32_t length = 0;
		int32_t caret = 0;
		int32_t anchor = 0;

		void set ( const wchar_t* value );

		void insert ( const wchar_t* value );

		void erase ( bool forward );

		void move ( int32_t to, bool extend );

		bool selection ( int32_t& from, int32_t& to ) const;

	};

	struct machine_form {

		text_field name;
		int32_t preset = 0;
		uint32_t memory = 640;
		wchar_t floppies [ 2 ] [ path_room ] = { };
		bool custom_bios = false;
		wchar_t bios [ path_room ] = { };

	};

	class manager {

	public:

		void layout ( float view_width, float view_height );

		void paint ( render::gpu& gpu, const manager_view& state, double now );

		void mouse_move ( float x, float y, double now );

		void mouse_leave ( );

		void mouse_down ( float x, float y, double now );

		manager_action mouse_up ( float x, float y, int32_t& index );

		void wheel ( float delta );

		manager_action key ( uint32_t code, bool shift, bool control, int32_t& index );

		void character ( wchar_t value );

		void paste ( const wchar_t* value );

		bool animating ( double now ) const;

		void open_sheet ( sheet_kind kind, double now );

		void close_sheet ( );

		void toast ( const wchar_t* text, double now );

		bool name_valid ( ) const;

		bool editing ( ) const {

			return sheet == SHEET_NEW || ( sheet == SHEET_SETTINGS && !subject_running );

		}

		sheet_kind sheet = SHEET_NONE;
		machine_form form;
		wchar_t subject [ 64 ] = { };
		bool subject_running = false;

	private:

		struct hit {

			render::rect area;
			manager_action action;
			int32_t index;

		};

		void add_hit ( render::rect area, manager_action action, int32_t index = 0 );

		const hit* find ( float x, float y ) const;

		float glow ( manager_action action, int32_t index ) const;

		bool button ( render::gpu& gpu, const theme& colors, render::rect area, const wchar_t* label, manager_action action, int32_t style, bool enabled, int32_t index = 0 );

		void badge ( render::gpu& gpu, const theme& colors, render::rect area, const wchar_t* mark, bool large );

		void field ( render::gpu& gpu, const theme& colors, render::rect area, double now );

		void segmented ( render::gpu& gpu, const theme& colors, render::rect area, const wchar_t* const* labels, int32_t count, int32_t selected, manager_action action, bool enabled );

		void paint_sidebar ( render::gpu& gpu, const theme& colors, const manager_view& state );

		void paint_empty ( render::gpu& gpu, const theme& colors, render::rect pane );

		void paint_detail ( render::gpu& gpu, const theme& colors, const machine_card& card, render::rect pane );

		void paint_sheet ( render::gpu& gpu, const theme& colors, const manager_view& state, double now );

		float paint_machine_fields ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled );

		int32_t caret_from ( float x ) const;

		hit hits [ 160 ];
		int32_t hit_count = 0;
		float width = 0.0f;
		float height = 0.0f;
		float sidebar = 288.0f;
		float scroll = 0.0f;
		float scroll_limit = 0.0f;
		manager_action hover_action = ACTION_NONE;
		int32_t hover_index = -1;
		manager_action press_action = ACTION_NONE;
		int32_t press_index = -1;
		float hover_amount = 0.0f;
		double last_paint = 0.0;
		double sheet_since = 0.0;
		float sheet_amount = 0.0f;
		sheet_kind shown = SHEET_NONE;
		render::rect sheet_area;
		render::rect field_area;
		bool field_focused = false;
		double caret_since = 0.0;
		render::gpu* measurer = nullptr;
		wchar_t toast_text [ 96 ] = { };
		double toast_since = -100.0;

	};

}