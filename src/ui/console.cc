#include "ui/console.hh"
#include "base/math.hh"

namespace ui {

	using render::color;
	using render::fade;
	using render::rect;

	static const wchar_t* memory_label ( base::writer& out, uint32_t megabytes ) {

		if ( megabytes >= 1024 && megabytes % 1024 == 0 )
			out.number ( megabytes / 1024 ).add ( L" GB" );
		else
			out.number ( megabytes ).add ( L" MB" );

		return out.text ( );

	}

	void console::layout ( float view_width, float view_height, bool fullscreen ) {

		width = view_width;
		height = view_height;
		full = fullscreen;

	}

	void console::add_hit ( rect area, console_action action ) {

		if ( hit_count < 32 )
			hits [ hit_count++ ] = { area, action };

	}

	console_action console::find ( float x, float y ) const {

		for ( int32_t i = hit_count - 1; i >= 0; i-- ) {

			if ( hits [ i ].area.contains ( x, y ) )
				return hits [ i ].action;

		}

		return TOOL_NONE;

	}

	void console::mouse_move ( float x, float y ) {

		hovered = find ( x, y );

	}

	void console::mouse_leave ( ) {

		hovered = TOOL_NONE;

	}

	void console::mouse_down ( float x, float y ) {

		pressed = find ( x, y );

	}

	console_action console::mouse_up ( float x, float y ) {

		console_action released = find ( x, y );
		console_action result = released == pressed ? released : TOOL_NONE;

		pressed = TOOL_NONE;

		return result;

	}

	bool console::animating ( double now ) const {

		return now - toast_since < 5.0;

	}

	void console::toast ( const wchar_t* text, double now ) {

		int32_t length = 0;

		for ( ; text [ length ] && length < 95; length++ )
			toast_text [ length ] = text [ length ];

		toast_text [ length ] = 0;
		toast_since = now;

	}

	float console::tool ( render::gpu& gpu, const theme& colors, float x, const wchar_t* label, render::icon_kind icon, console_action action, bool enabled, bool accent, bool compact ) {

		float text_width = compact ? 0.0f : gpu.measure ( label, render::FONT_BODY );
		rect area = { x, math::round ( ( toolbar - 30.0f ) * 0.5f ), compact ? 36.0f : 10.0f + 16.0f + 7.0f + text_width + 12.0f, 30.0f };
		color tint = enabled ? colors.text : colors.faint;

		if ( enabled && hovered == action )
			gpu.fill ( area, corner, pressed == action ? colors.pressed : colors.hover );

		gpu.icon ( icon, area.x + 18.0f, area.y + 15.0f, 15.0f, enabled ? ( accent ? colors.accent : colors.muted ) : colors.faint );

		if ( !compact )
			gpu.text ( label, render::FONT_BODY, rect { area.x + 33.0f, area.y, text_width + 4.0f, area.h }, tint, render::ALIGN_LEFT );

		if ( enabled )
			add_hit ( area, action );

		return area.right ( ) + 2.0f;

	}

	void console::paint ( render::gpu& gpu, const console_view& state, double now ) {

		theme colors = state.light ? light_theme ( ) : dark_theme ( );

		hit_count = 0;

		if ( !gpu.begin ( colors.background ) )
			return;

		paint_screen ( gpu, colors, state );

		if ( !full ) {

			paint_toolbar ( gpu, colors, state );
			paint_status ( gpu, colors, state, now );

		} else {

			double age = now - toast_since;

			if ( age < 5.0 && toast_text [ 0 ] ) {

				float alpha = static_cast < float > ( base::min ( base::min ( age / 0.12, 1.0 ), ( 5.0 - age ) / 0.6 ) );
				float text_width = gpu.measure ( toast_text, render::FONT_BODY );
				rect pill = { math::round ( ( width - text_width - 32.0f ) * 0.5f ), 24.0f, text_width + 32.0f, 34.0f };

				gpu.opacity = base::clamp ( alpha, 0.0f, 1.0f );
				gpu.fill ( pill, corner_large, colors.raised );
				gpu.stroke ( pill, corner_large, 1.0f, colors.divider );
				gpu.text ( toast_text, render::FONT_BODY, pill, colors.text, render::ALIGN_CENTER );
				gpu.opacity = 1.0f;

			}

		}

		bool still = false;

		for ( int32_t i = 0; i < hit_count; i++ )
			still = still || hits [ i ].action == hovered;

		if ( !still )
			hovered = TOOL_NONE;

	}

	void console::paint_toolbar ( render::gpu& gpu, const theme& colors, const console_view& state ) {

		struct entry {

			const wchar_t* label;
			render::icon_kind icon;
			console_action action;
			bool enabled;
			bool accent;
			bool divided;

		};

		bool disc = state.disc [ 0 ] != 0;
		bool on = state.running;
		entry items [ 14 ];
		int32_t count = 0;

		if ( on ) {

			items [ count++ ] = { L"Shut down", render::ICON_POWER, TOOL_SHUT_DOWN, true, false, false };
			items [ count++ ] = { L"Turn off", render::ICON_POWER, TOOL_TURN_OFF, true, false, false };
			items [ count++ ] = { L"Reset", render::ICON_RESET, TOOL_RESET, true, false, false };
			items [ count++ ] = { L"Ctrl+Alt+Del", render::ICON_KEYBOARD, TOOL_KEYS, true, false, false };

		} else
			items [ count++ ] = { L"Start", render::ICON_PLAY, TOOL_START, true, true, false };

		if ( state.can_save )
			items [ count++ ] = { L"Save state", render::ICON_SAVE, TOOL_SAVE, on, false, false };

		items [ count++ ] = { disc ? L"Change disc" : L"Insert disc", render::ICON_DISK, TOOL_DISC, true, false, true };

		if ( disc )
			items [ count++ ] = { L"Eject", render::ICON_EJECT, TOOL_EJECT, true, false, false };

		items [ count++ ] = { L"Send files", render::ICON_FOLDER, TOOL_FILES, on, false, true };
		items [ count++ ] = { L"Type text", render::ICON_KEYBOARD, TOOL_TYPE, on, false, false };

		if ( on && !state.tools_linked )
			items [ count++ ] = { L"Install tools", render::ICON_PLUS, TOOL_TOOLS, true, false, false };
		items [ count++ ] = { L"Screenshot", render::ICON_CAMERA, TOOL_SHOT, on && state.frame_width > 0, false, true };
		items [ count++ ] = { L"Fullscreen", render::ICON_EXPAND, TOOL_FULLSCREEN, true, false, false };

		float needed = 16.0f;

		for ( int32_t i = 0; i < count; i++ )
			needed += 47.0f + gpu.measure ( items [ i ].label, render::FONT_BODY ) + ( items [ i ].divided ? 14.0f : 0.0f );

		bool compact = needed > width;
		float x = 8.0f;

		gpu.fill ( rect { 0.0f, 0.0f, width, toolbar }, 0.0f, colors.chrome );
		gpu.fill ( rect { 0.0f, toolbar - 1.0f, width, 1.0f }, 0.0f, colors.divider );

		for ( int32_t i = 0; i < count; i++ ) {

			if ( items [ i ].divided ) {

				gpu.fill ( rect { x + 6.0f, 13.0f, 1.0f, toolbar - 26.0f }, 0.0f, colors.divider );
				x += 14.0f;

			}

			x = tool ( gpu, colors, x, items [ i ].label, items [ i ].icon, items [ i ].action, items [ i ].enabled, items [ i ].accent, compact );

		}

	}

	void console::paint_screen ( render::gpu& gpu, const theme& colors, const console_view& state ) {

		rect area = full ? rect { 0.0f, 0.0f, width, height } : rect { 0.0f, toolbar, width, height - toolbar - status };

		gpu.fill ( area, 0.0f, colors.bezel );
		frame_area = { };

		if ( state.running && state.pixels && state.frame_width > 0 && state.frame_height > 0 ) {

			float aspect = static_cast < float > ( state.frame_width ) / static_cast < float > ( state.frame_height );

			if ( ( state.frame_width == 720 && state.frame_height == 400 ) || ( state.frame_width == 640 && state.frame_height == 400 ) || state.frame_height == 200 || ( state.frame_width == 360 && state.frame_height == 400 ) )
				aspect = 4.0f / 3.0f;

			float target_width = area.w;
			float target_height = target_width / aspect;

			if ( target_height > area.h ) {

				target_height = area.h;
				target_width = target_height * aspect;

			}

			frame_area = { math::round ( area.x + ( area.w - target_width ) * 0.5f ), math::round ( area.y + ( area.h - target_height ) * 0.5f ), math::round ( target_width ), math::round ( target_height ) };
			gpu.screen ( state.pixels, state.frame_width, state.frame_height, frame_area, false );
			add_hit ( frame_area, TOOL_SCREEN );

			return;

		}

		if ( state.running )
			return;

		float block = 360.0f;
		float x = area.x + math::round ( ( area.w - block ) * 0.5f );
		float y = area.y + math::round ( area.h * 0.5f - 70.0f );
		color quiet = render::rgb ( 0x9A9DA4 );

		gpu.icon ( render::ICON_MONITOR, x + 16.0f, y + 16.0f, 30.0f, render::rgb ( 0x5F6269 ) );
		gpu.text ( state.failed ? L"The machine stopped" : L"The machine is off", render::FONT_HEADING, rect { x, y + 44.0f, block, 26.0f }, render::rgb ( 0xE4E5E7 ), render::ALIGN_LEFT );

		if ( state.failed && state.problem [ 0 ] )
			gpu.text ( state.problem, render::FONT_BODY, rect { x, y + 74.0f, block, 40.0f }, colors.danger, render::ALIGN_LEFT );
		else
			gpu.text ( L"Start it to boot from the disc or the hard disk.", render::FONT_BODY, rect { x, y + 76.0f, block, 20.0f }, quiet, render::ALIGN_LEFT );

		rect start = { x, y + 122.0f, 120.0f, 32.0f };

		gpu.fill ( start, corner, colors.accent );

		if ( hovered == TOOL_START )
			gpu.fill ( start, corner, colors.hover );

		gpu.icon ( render::ICON_PLAY, start.x + 26.0f, start.y + 16.0f, 14.0f, colors.accent_text );
		gpu.text ( L"Start", render::FONT_STRONG, rect { start.x + 40.0f, start.y, 70.0f, start.h }, colors.accent_text, render::ALIGN_LEFT );
		add_hit ( start, TOOL_START );

	}

	void console::paint_status ( render::gpu& gpu, const theme& colors, const console_view& state, double now ) {

		rect bar = { 0.0f, height - status, width, status };
		base::writer summary;
		base::writer memory;

		gpu.fill ( bar, 0.0f, colors.chrome );
		gpu.fill ( rect { 0.0f, bar.y, width, 1.0f }, 0.0f, colors.divider );
		gpu.circle ( 14.0f, bar.y + status * 0.5f, 3.5f, state.running ? colors.led_green : ( state.failed ? colors.led_red : colors.led_off ) );
		summary.add ( state.running ? L"Running" : ( state.failed ? L"Stopped" : L"Off" ) );
		summary.add ( L"  \x00B7  " ).number ( state.processors ).add ( state.processors == 1 ? L" CPU" : L" CPUs" );
		summary.add ( L"  \x00B7  " ).add ( memory_label ( memory, state.memory_mb ) );

		float x = 24.0f;
		float summary_width = gpu.measure ( summary.text ( ), render::FONT_SMALL );

		gpu.text ( summary.text ( ), render::FONT_SMALL, rect { x, bar.y, summary_width + 4.0f, status }, colors.muted, render::ALIGN_LEFT );
		x += summary_width + 18.0f;
		gpu.fill ( rect { x - 9.0f, bar.y + 8.0f, 1.0f, status - 16.0f }, 0.0f, colors.divider );

		auto drive = [ & ] ( const wchar_t* label, const wchar_t* name, bool light ) {

			base::writer text;

			text.add ( label ).add ( L"  " ).add ( name [ 0 ] ? name : L"empty" );

			float text_width = base::min ( gpu.measure ( text.text ( ), render::FONT_SMALL ), 260.0f );

			gpu.circle ( x + 3.0f, bar.y + status * 0.5f, 3.0f, light ? colors.accent : colors.led_off );
			gpu.text ( text.text ( ), render::FONT_SMALL, rect { x + 12.0f, bar.y, text_width + 4.0f, status }, name [ 0 ] ? colors.muted : colors.faint, render::ALIGN_LEFT );
			x += 12.0f + text_width + 18.0f;

		};

		drive ( L"DVD", state.disc, state.disc_light );

		if ( state.disk [ 0 ] )
			drive ( L"Disk", state.disk, state.disk_light );

		double age = now - toast_since;

		if ( age < 5.0 && toast_text [ 0 ] ) {

			float alpha = static_cast < float > ( base::min ( base::min ( age / 0.12, 1.0 ), ( 5.0 - age ) / 0.6 ) );

			gpu.opacity = base::clamp ( alpha, 0.0f, 1.0f );
			gpu.fill ( rect { x - 9.0f, bar.y + 8.0f, 1.0f, status - 16.0f }, 0.0f, colors.divider );
			gpu.text ( toast_text, render::FONT_SMALL, rect { x, bar.y, width * 0.4f, status }, colors.text, render::ALIGN_LEFT );
			gpu.opacity = 1.0f;

		}

		base::writer right;

		if ( state.running && state.frame_width > 0 )
			right.number ( state.frame_width ).add ( L" \x00D7 " ).number ( state.frame_height ).add ( L"  \x00B7  " );

		if ( state.tools_linked )
			right.add ( L"Tools connected  \x00B7  " );

		right.add ( state.captured ? L"Right Ctrl releases the mouse and keyboard" : ( state.integrated ? L"Mouse integration on" : L"Click the screen to capture the mouse" ) );
		gpu.text ( right.text ( ), render::FONT_SMALL, rect { width * 0.45f, bar.y, width * 0.55f - 14.0f, status }, state.captured ? colors.accent : colors.faint, render::ALIGN_RIGHT );

	}

}