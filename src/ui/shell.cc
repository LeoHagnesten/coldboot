#include "ui/shell.hh"
#include "base/math.hh"

namespace ui {

	using render::rect;
	using render::color;
	using render::fade;

	static const wchar_t* tooltip_text ( control id ) {

		switch ( id ) {

			case CONTROL_POWER:
				return L"Power";

			case CONTROL_RESET:
				return L"Reset";

			case CONTROL_PAUSE:
				return L"Pause  \x00B7  Pause key";

			case CONTROL_SAVE:
				return L"Save state";

			case CONTROL_LOAD:
				return L"Load state";

			case CONTROL_SHOT:
				return L"Screenshot  \x00B7  F12";

			case CONTROL_SPEED:
				return L"Switch between 4.77 MHz and turbo";

			case CONTROL_DISKS:
				return L"Floppy drives";

			case CONTROL_SETTINGS:
				return L"Settings";

			case CONTROL_FULLSCREEN:
				return L"Fullscreen  \x00B7  F11";

			default:
				return nullptr;

		}

	}

	static float approach ( float value, float target, float dt, float rate ) {

		float step = base::min ( 1.0f, dt * rate );

		return value + ( target - value ) * step;

	}

	void shell::layout ( float view_width, float view_height, bool fullscreen ) {

		width = view_width;
		height = view_height;
		full = fullscreen;

		float top = full ? 0.0f : toolbar;
		float bottom = full ? height : height - status;
		float margin = full ? 0.0f : 14.0f;
		float room_width = base::max ( width - margin * 2.0f, 1.0f );
		float room_height = base::max ( bottom - top - margin * 2.0f, 1.0f );
		float screen_width = room_width;
		float screen_height = screen_width * 3.0f / 4.0f;

		if ( screen_height > room_height ) {

			screen_height = room_height;
			screen_width = screen_height * 4.0f / 3.0f;

		}

		screen_area = rect { math::round ( ( width - screen_width ) * 0.5f ), math::round ( top + margin + ( room_height - screen_height ) * 0.5f ), math::round ( screen_width ), math::round ( screen_height ) };

	}

	void shell::add_hit ( rect area, control id ) {

		if ( hit_count < 96 )
			hits [ hit_count++ ] = hit { area, id };

	}

	control shell::find ( float x, float y ) const {

		for ( int32_t i = hit_count - 1; i >= 0; i-- ) {

			if ( hits [ i ].area.contains ( x, y ) )
				return hits [ i ].id;

		}

		return CONTROL_NONE;

	}

	void shell::mouse_move ( float x, float y, double now ) {

		mouse_x = x;
		mouse_y = y;

		control found = find ( x, y );

		if ( found != hovered ) {

			hovered = found;
			hover_since = now;

		}

	}

	void shell::mouse_leave ( ) {

		hovered = CONTROL_NONE;
		mouse_x = -1.0f;
		mouse_y = -1.0f;

	}

	void shell::mouse_down ( float x, float y ) {

		pressed = find ( x, y );

	}

	control shell::mouse_up ( float x, float y ) {

		control found = find ( x, y );
		control was = pressed;

		pressed = CONTROL_NONE;

		if ( popover != POPOVER_NONE && !popover_area.contains ( x, y ) && found != CONTROL_DISKS && found != CONTROL_SETTINGS && found != CONTROL_DRIVE_A && found != CONTROL_DRIVE_B ) {

			popover = POPOVER_NONE;

			return CONTROL_NONE;

		}

		if ( found != was )
			return CONTROL_NONE;

		if ( found == CONTROL_DISKS || found == CONTROL_DRIVE_A || found == CONTROL_DRIVE_B ) {

			popover = popover == POPOVER_DISKS ? POPOVER_NONE : POPOVER_DISKS;
			popover_since = last_paint;

			return CONTROL_NONE;

		}

		if ( found == CONTROL_SETTINGS ) {

			popover = popover == POPOVER_SETTINGS ? POPOVER_NONE : POPOVER_SETTINGS;
			popover_since = last_paint;

			return CONTROL_NONE;

		}

		return found;

	}

	void shell::open_popover ( popover_kind kind, double now ) {

		popover = kind;
		popover_since = now;

	}

	void shell::toast ( const wchar_t* text, double now ) {

		int32_t length = 0;

		for ( ; text [ length ] && length < 95; length++ )
			toast_text [ length ] = text [ length ];

		toast_text [ length ] = 0;
		toast_since = now;

	}

	bool shell::animating ( double now ) const {

		for ( int32_t i = 0; i < CONTROL_COUNT; i++ ) {

			float target = i == hovered ? 1.0f : 0.0f;

			if ( math::abs ( glows [ i ] - target ) > 0.01f )
				return true;

		}

		float target = popover != POPOVER_NONE ? 1.0f : 0.0f;

		if ( math::abs ( popover_amount - target ) > 0.01f )
			return true;

		if ( now - toast_since < 2.6 )
			return true;

		if ( hovered != CONTROL_NONE && tooltip_text ( hovered ) && now - hover_since < 0.7 )
			return true;

		return false;

	}

	bool shell::button ( render::gpu& gpu, const theme& colors, rect area, control id, render::icon_kind icon, bool active, bool enabled, color tint ) {

		anchors [ id ] = area;

		float amount = glows [ id ];

		if ( active )
			gpu.fill ( area, corner, fade ( colors.accent, 0.16f ) );

		if ( enabled && amount > 0.01f )
			gpu.fill ( area, corner, fade ( pressed == id ? colors.pressed : colors.hover, amount ) );

		gpu.icon ( icon, area.x + area.w * 0.5f, area.y + area.h * 0.5f, 18.0f, enabled ? ( active ? colors.accent : tint ) : colors.faint );

		if ( enabled )
			add_hit ( area, id );

		return enabled;

	}

	void shell::segmented ( render::gpu& gpu, const theme& colors, rect area, const control* ids, const wchar_t* const* labels, int32_t count, int32_t selected ) {

		gpu.fill ( area, corner, colors.inset );
		gpu.stroke ( area, corner, 1.0f, colors.divider );

		float part = ( area.w - 4.0f ) / static_cast < float > ( count );

		for ( int32_t i = 0; i < count; i++ ) {

			rect segment = { area.x + 2.0f + part * static_cast < float > ( i ), area.y + 2.0f, part, area.h - 4.0f };

			if ( i == selected ) {

				gpu.fill ( segment, corner - 1.0f, colors.raised );
				gpu.stroke ( segment, corner - 1.0f, 1.0f, colors.divider );

			} else if ( glows [ ids [ i ] ] > 0.01f )
				gpu.fill ( segment, corner - 1.0f, fade ( colors.hover, glows [ ids [ i ] ] ) );

			gpu.text ( labels [ i ], i == selected ? render::FONT_SMALL_STRONG : render::FONT_SMALL, segment, i == selected ? colors.text : colors.muted, render::ALIGN_CENTER );
			add_hit ( segment, ids [ i ] );

		}

	}

	void shell::paint ( render::gpu& gpu, const view& state, double now ) {

		theme colors = state.light ? light_theme ( ) : dark_theme ( );
		float dt = static_cast < float > ( base::clamp ( now - last_paint, 0.0, 0.1 ) );

		last_paint = now;

		for ( int32_t i = 0; i < CONTROL_COUNT; i++ )
			glows [ i ] = approach ( glows [ i ], i == hovered ? 1.0f : 0.0f, dt, 16.0f );

		popover_amount = approach ( popover_amount, popover != POPOVER_NONE ? 1.0f : 0.0f, dt, 18.0f );
		hit_count = 0;

		if ( !gpu.begin ( full ? render::rgb ( 0x000000 ) : colors.background ) )
			return;
		paint_screen ( gpu, colors, state, now );

		if ( !full ) {

			paint_toolbar ( gpu, colors, state );
			paint_status ( gpu, colors, state );

		}

		if ( popover != POPOVER_NONE )
			shown = popover;

		if ( popover_amount > 0.01f && !full && shown != POPOVER_NONE ) {

			bool disks = shown == POPOVER_DISKS;
			rect panel = disks ? anchor_panel ( CONTROL_DISKS, 384.0f, 306.0f ) : anchor_panel ( CONTROL_SETTINGS, 360.0f, 346.0f );
			float lift = ( 1.0f - popover_amount ) * 8.0f;

			panel.y -= lift;
			popover_area = panel;
			gpu.opacity = popover_amount;
			gpu.shadow ( panel, corner_large, 16.0f, colors.shadow );
			gpu.fill ( panel, corner_large, colors.raised );
			gpu.stroke ( panel, corner_large, 1.0f, colors.divider );
			add_hit ( panel, CONTROL_NONE );

			if ( disks )
				paint_disks ( gpu, colors, state, panel );
			else
				paint_settings ( gpu, colors, state, panel );

			gpu.opacity = 1.0f;

		}

		double age = now - toast_since;

		if ( age < 2.4 && toast_text [ 0 ] ) {

			float alpha = static_cast < float > ( base::min ( base::min ( age / 0.15, 1.0 ), base::min ( ( 2.4 - age ) / 0.4, 1.0 ) ) );
			float text_width = gpu.measure ( toast_text, render::FONT_BODY );
			rect pill = { math::round ( ( width - text_width - 36.0f ) * 0.5f ), full ? height - 64.0f : height - status - 54.0f, text_width + 36.0f, 34.0f };

			gpu.opacity = alpha;
			gpu.shadow ( pill, corner, 10.0f, colors.shadow );
			gpu.fill ( pill, corner, colors.raised );
			gpu.stroke ( pill, corner, 1.0f, colors.divider );
			gpu.text ( toast_text, render::FONT_BODY, pill, colors.text, render::ALIGN_CENTER );
			gpu.opacity = 1.0f;

		}

		paint_tooltip ( gpu, colors, now );

	}

	rect shell::anchor_panel ( control anchor, float panel_width, float panel_height ) const {

		rect source = anchors [ anchor ];
		float x = base::clamp ( source.right ( ) - panel_width, 8.0f, base::max ( width - panel_width - 8.0f, 8.0f ) );

		return rect { x, toolbar + 6.0f, panel_width, panel_height };

	}

	void shell::paint_toolbar ( render::gpu& gpu, const theme& colors, const view& state ) {

		gpu.fill ( rect { 0.0f, 0.0f, width, toolbar }, 0.0f, colors.chrome );
		gpu.fill ( rect { 0.0f, toolbar - 1.0f, width, 1.0f }, 0.0f, colors.divider );

		float size = 36.0f;
		float y = ( toolbar - size ) * 0.5f;
		float x = 10.0f;
		auto next = [ & ] ( float gap ) {

			rect area = { x, y, size, size };

			x += size + gap;

			return area;

		};
		auto separator = [ & ] ( ) {

			gpu.fill ( rect { x + 3.0f, y + 9.0f, 1.0f, size - 18.0f }, 0.0f, colors.divider );
			x += 11.0f;

		};

		button ( gpu, colors, next ( 2.0f ), CONTROL_POWER, render::ICON_POWER, false, true, state.powered ? colors.led_green : colors.text );
		button ( gpu, colors, next ( 2.0f ), CONTROL_RESET, render::ICON_RESET, false, state.powered, colors.text );
		button ( gpu, colors, next ( 2.0f ), CONTROL_PAUSE, state.paused ? render::ICON_PLAY : render::ICON_PAUSE, state.paused, state.powered, colors.text );
		separator ( );
		button ( gpu, colors, next ( 2.0f ), CONTROL_SAVE, render::ICON_SAVE, false, state.powered, colors.text );
		button ( gpu, colors, next ( 2.0f ), CONTROL_LOAD, render::ICON_LOAD, false, state.powered && state.can_load, colors.text );
		separator ( );
		button ( gpu, colors, next ( 2.0f ), CONTROL_SHOT, render::ICON_CAMERA, false, state.powered, colors.text );

		float left_end = x;
		float right = width - 10.0f;

		x = right - size;
		button ( gpu, colors, rect { x, y, size, size }, CONTROL_FULLSCREEN, render::ICON_EXPAND, false, true, colors.text );
		x -= size + 2.0f;
		button ( gpu, colors, rect { x, y, size, size }, CONTROL_SETTINGS, render::ICON_SLIDERS, popover == POPOVER_SETTINGS, true, colors.text );
		x -= size + 2.0f;
		button ( gpu, colors, rect { x, y, size, size }, CONTROL_DISKS, render::ICON_DISK, popover == POPOVER_DISKS, true, colors.text );

		const wchar_t* speed_label = state.turbo ? L"Turbo" : L"4.77 MHz";
		float pill_width = gpu.measure ( speed_label, render::FONT_SMALL_STRONG ) + 40.0f;
		rect pill = { x - pill_width - 8.0f, y + 3.0f, pill_width, size - 6.0f };
		float amount = glows [ CONTROL_SPEED ];

		anchors [ CONTROL_SPEED ] = pill;
		gpu.fill ( pill, corner, state.turbo ? fade ( colors.accent, 0.18f ) : colors.inset );

		if ( amount > 0.01f )
			gpu.fill ( pill, corner, fade ( colors.hover, amount ) );

		gpu.icon ( render::ICON_BOLT, pill.x + 16.0f, pill.y + pill.h * 0.5f, 14.0f, state.turbo ? colors.accent : colors.muted );
		gpu.text ( speed_label, render::FONT_SMALL_STRONG, rect { pill.x + 28.0f, pill.y, pill.w - 34.0f, pill.h }, state.turbo ? colors.accent : colors.text, render::ALIGN_LEFT );
		add_hit ( pill, CONTROL_SPEED );

		float title_width = gpu.measure ( state.title, render::FONT_STRONG );
		base::writer detail;

		detail.add ( L"IBM PC/XT  \x00B7  8088  \x00B7  " ).number ( state.memory ).add ( L" KB" );

		float detail_width = gpu.measure ( detail.text ( ), render::FONT_SMALL );
		float block = title_width + 12.0f + detail_width;
		float center = math::round ( ( width - block ) * 0.5f );

		if ( center > left_end + 16.0f && center + block < pill.x - 16.0f ) {

			gpu.text ( state.title, render::FONT_STRONG, rect { center, 0.0f, title_width + 2.0f, toolbar }, colors.text, render::ALIGN_LEFT );
			gpu.text ( detail.text ( ), render::FONT_SMALL, rect { center + title_width + 12.0f, 0.0f, detail_width + 2.0f, toolbar }, colors.muted, render::ALIGN_LEFT );

		}

	}

	void shell::paint_status ( render::gpu& gpu, const theme& colors, const view& state ) {

		float top = height - status;

		gpu.fill ( rect { 0.0f, top, width, status }, 0.0f, colors.chrome );
		gpu.fill ( rect { 0.0f, top, width, 1.0f }, 0.0f, colors.divider );

		float x = 16.0f;
		float middle = top + status * 0.5f;
		color led = state.powered ? ( state.paused ? colors.accent : colors.led_green ) : ( state.failed ? colors.danger : colors.led_off );
		const wchar_t* label = state.powered ? ( state.paused ? L"Paused" : L"Running" ) : ( state.failed ? L"Couldn't start" : L"Off" );

		gpu.circle ( x + 4.0f, middle, 4.0f, led );
		x += 16.0f;

		float label_width = gpu.measure ( label, render::FONT_SMALL );

		gpu.text ( label, render::FONT_SMALL, rect { x, top, label_width + 2.0f, status }, colors.text, render::ALIGN_LEFT );
		x += label_width + 14.0f;

		if ( state.powered && state.post_code && state.post_code < 0x0D ) {

			wchar_t chip [ 16 ] = L"POST ";
			const wchar_t* digits = L"0123456789ABCDEF";

			chip [ 5 ] = digits [ state.post_code >> 4 ];
			chip [ 6 ] = digits [ state.post_code & 15 ];
			chip [ 7 ] = 0;

			float chip_width = gpu.measure ( chip, render::FONT_MONO ) + 16.0f;
			rect area = { x, middle - 10.0f, chip_width, 20.0f };

			gpu.fill ( area, corner, colors.inset );
			gpu.text ( chip, render::FONT_MONO, area, colors.accent, render::ALIGN_CENTER );
			x += chip_width + 14.0f;

		}

		gpu.fill ( rect { x, top + 9.0f, 1.0f, status - 18.0f }, 0.0f, colors.divider );
		x += 12.0f;

		for ( int32_t d = 0; d < 2; d++ ) {

			const drive_view& drive = state.drives [ d ];
			const wchar_t* name = drive.present ? drive.name : L"empty";
			float name_width = base::min ( gpu.measure ( name, render::FONT_SMALL ), 220.0f );
			rect chip = { x, middle - 11.0f, name_width + 46.0f, 22.0f };
			control id = d ? CONTROL_DRIVE_B : CONTROL_DRIVE_A;

			if ( glows [ id ] > 0.01f )
				gpu.fill ( chip, corner, fade ( colors.hover, glows [ id ] ) );

			gpu.circle ( chip.x + 9.0f, middle, 3.0f, drive.light ? colors.led_red : ( drive.present ? colors.muted : colors.led_off ) );
			gpu.text ( d ? L"B:" : L"A:", render::FONT_SMALL_STRONG, rect { chip.x + 18.0f, top, 20.0f, status }, colors.text, render::ALIGN_LEFT );
			gpu.text ( name, render::FONT_SMALL, rect { chip.x + 36.0f, top, name_width + 2.0f, status }, drive.present ? colors.muted : colors.faint, render::ALIGN_LEFT );
			add_hit ( chip, id );
			x += chip.w + 6.0f;

		}

		base::writer speed;

		if ( state.powered && !state.paused ) {

			speed.fixed ( state.speed, 2 ).add ( L" MHz  \x00B7  " ).number ( static_cast < int64_t > ( state.speed / 4.772727 * 100.0 + 0.5 ) ).add ( L"%" );

		}

		float speed_width = gpu.measure ( speed.text ( ), render::FONT_MONO );
		float right = width - 16.0f;

		gpu.text ( speed.text ( ), render::FONT_MONO, rect { right - speed_width - 2.0f, top, speed_width + 2.0f, status }, colors.muted, render::ALIGN_RIGHT );
		right -= speed_width + 18.0f;

		if ( state.captured ) {

			const wchar_t* hint = L"Keyboard captured  \x00B7  Right Ctrl releases";
			float hint_width = gpu.measure ( hint, render::FONT_SMALL );

			if ( right - hint_width > x + 12.0f ) {

				gpu.icon ( render::ICON_KEYBOARD, right - hint_width - 14.0f, middle, 15.0f, colors.accent );
				gpu.text ( hint, render::FONT_SMALL, rect { right - hint_width, top, hint_width + 2.0f, status }, colors.accent, render::ALIGN_LEFT );

			}

		}

	}

	void shell::paint_screen ( render::gpu& gpu, const theme& colors, const view& state, double now ) {

		rect area = screen_area;
		float radius = full ? 0.0f : 8.0f;

		gpu.fill ( area, radius, colors.bezel );

		if ( state.powered && state.pixels && state.frame_width > 0 ) {

			gpu.screen ( state.pixels, state.frame_width, state.frame_height, area, state.crt );

			if ( radius > 0.0f )
				gpu.corners ( area, radius, colors.background );

			add_hit ( area, CONTROL_SCREEN );

			return;

		}

		float cx = area.x + area.w * 0.5f;
		float cy = area.y + area.h * 0.5f - 22.0f;
		rect knob = { cx - 34.0f, cy - 34.0f, 68.0f, 68.0f };
		float amount = glows [ CONTROL_START ];
		color ring = state.failed ? colors.danger : colors.accent;

		gpu.circle ( cx, cy, 34.0f, fade ( ring, 0.10f + amount * 0.08f ) );
		gpu.ring ( cx, cy, 33.0f, 1.5f, fade ( ring, 0.55f + amount * 0.45f ) );
		gpu.icon ( render::ICON_POWER, cx, cy, 30.0f, ring );
		add_hit ( knob, CONTROL_START );

		const wchar_t* title = state.failed ? L"Coldboot couldn't start" : L"Power on";

		gpu.text ( title, render::FONT_TITLE, rect { area.x, cy + 44.0f, area.w, 24.0f }, render::rgb ( 0xEDEDED ), render::ALIGN_CENTER );

		base::writer detail;

		if ( state.failed && state.problem [ 0 ] )
			detail.add ( state.problem );
		else {

			detail.add ( state.bios_name ).add ( L"  \x00B7  " ).number ( state.memory ).add ( L" KB  \x00B7  " );

			if ( state.drives [ 0 ].present )
				detail.add ( L"boots " ).add ( state.drives [ 0 ].name );
			else
				detail.add ( L"drop a floppy image here to boot it" );

		}

		gpu.text ( detail.text ( ), render::FONT_SMALL, rect { area.x + 24.0f, cy + 70.0f, area.w - 48.0f, 20.0f }, render::rgb ( 0x8A8D93 ), render::ALIGN_CENTER );

	}

	void shell::paint_tooltip ( render::gpu& gpu, const theme& colors, double now ) {

		const wchar_t* label = tooltip_text ( hovered );

		if ( !label || full || popover != POPOVER_NONE || pressed != CONTROL_NONE || now - hover_since < 0.6 )
			return;

		float alpha = static_cast < float > ( base::min ( ( now - hover_since - 0.6 ) / 0.12, 1.0 ) );
		float label_width = gpu.measure ( label, render::FONT_SMALL );
		rect source = anchors [ hovered ];
		rect tip = { source.x + source.w * 0.5f - ( label_width + 20.0f ) * 0.5f, source.bottom ( ) + 8.0f, label_width + 20.0f, 26.0f };

		tip.x = base::clamp ( tip.x, 6.0f, base::max ( width - tip.w - 6.0f, 6.0f ) );
		gpu.opacity = alpha;
		gpu.shadow ( tip, corner, 6.0f, colors.shadow );
		gpu.fill ( tip, corner, colors.raised );
		gpu.stroke ( tip, corner, 1.0f, colors.divider );
		gpu.text ( label, render::FONT_SMALL, tip, colors.text, render::ALIGN_CENTER );
		gpu.opacity = 1.0f;

	}

}