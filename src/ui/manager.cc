#include "ui/manager.hh"
#include "base/math.hh"
#include <windows.h>

namespace ui {

	using render::rect;
	using render::color;
	using render::fade;

	static constexpr float row_height = 44.0f;
	static constexpr float list_header = 38.0f;
	static constexpr float label_column = 104.0f;
	static constexpr float control_height = 30.0f;

	static const uint32_t memory_choices [ 3 ] = { 256, 512, 640 };

	static float approach ( float value, float target, float dt, float rate ) {

		return value + ( target - value ) * base::min ( 1.0f, dt * rate );

	}

	static rect clip_to ( rect area, rect bounds ) {

		float top = base::max ( area.y, bounds.y );
		float bottom = base::min ( area.bottom ( ), bounds.bottom ( ) );

		return rect { area.x, top, area.w, base::max ( bottom - top, 0.0f ) };

	}

	static const wchar_t* file_name ( const wchar_t* path ) {

		const wchar_t* name = path;

		for ( const wchar_t* at = path; *at; at++ ) {

			if ( *at == L'\\' || *at == L'/' )
				name = at + 1;

		}

		return name;

	}

	void text_field::set ( const wchar_t* value ) {

		length = 0;

		for ( ; value && value [ length ] && length < room; length++ )
			text [ length ] = value [ length ];

		text [ length ] = 0;
		caret = length;
		anchor = length;

	}

	bool text_field::selection ( int32_t& from, int32_t& to ) const {

		from = base::min ( caret, anchor );
		to = base::max ( caret, anchor );

		return from != to;

	}

	void text_field::insert ( const wchar_t* value ) {

		int32_t from = 0;
		int32_t to = 0;

		if ( selection ( from, to ) )
			erase ( false );

		for ( int32_t i = 0; value [ i ] && length < room; i++ ) {

			if ( value [ i ] < 32 )
				continue;

			for ( int32_t j = length; j > caret; j-- )
				text [ j ] = text [ j - 1 ];

			text [ caret++ ] = value [ i ];
			length++;

		}

		text [ length ] = 0;
		anchor = caret;

	}

	void text_field::erase ( bool forward ) {

		int32_t from = 0;
		int32_t to = 0;

		if ( !selection ( from, to ) ) {

			if ( forward && caret < length ) {

				from = caret;
				to = caret + 1;

			} else if ( !forward && caret > 0 ) {

				from = caret - 1;
				to = caret;

			} else
				return;

		}

		for ( int32_t i = to; i <= length; i++ )
			text [ from + i - to ] = text [ i ];

		length -= to - from;
		caret = from;
		anchor = from;

	}

	void text_field::move ( int32_t to, bool extend ) {

		caret = base::clamp ( to, 0, length );

		if ( !extend )
			anchor = caret;

	}

	void manager::layout ( float view_width, float view_height ) {

		width = view_width;
		height = view_height;
		sidebar = base::clamp ( width * 0.24f, 240.0f, 300.0f );

	}

	void manager::add_hit ( rect area, manager_action action, int32_t index ) {

		if ( hit_count < 160 && area.w > 0.0f && area.h > 0.0f )
			hits [ hit_count++ ] = hit { area, action, index };

	}

	const manager::hit* manager::find ( float x, float y ) const {

		for ( int32_t i = hit_count - 1; i >= 0; i-- ) {

			if ( hits [ i ].area.contains ( x, y ) )
				return &hits [ i ];

		}

		return nullptr;

	}

	float manager::glow ( manager_action action, int32_t index ) const {

		return hover_action == action && hover_index == index ? hover_amount : 0.0f;

	}

	void manager::mouse_move ( float x, float y, double ) {

		const hit* found = find ( x, y );
		manager_action action = found ? found->action : ACTION_NONE;
		int32_t index = found ? found->index : -1;

		if ( action != hover_action || index != hover_index ) {

			hover_action = action;
			hover_index = index;
			hover_amount = 0.0f;

		}

	}

	void manager::mouse_leave ( ) {

		hover_action = ACTION_NONE;
		hover_index = -1;

	}

	int32_t manager::caret_from ( float x ) const {

		if ( !measurer )
			return form.name.length;

		float target = x - ( field_area.x + 10.0f );
		int32_t best = 0;
		float distance = 1e9f;
		wchar_t prefix [ text_field::room + 1 ];

		for ( int32_t i = 0; i <= form.name.length; i++ ) {

			for ( int32_t j = 0; j < i; j++ )
				prefix [ j ] = form.name.text [ j ];

			prefix [ i ] = 0;

			float offset = i ? measurer->measure ( prefix, render::FONT_BODY ) : 0.0f;

			if ( math::abs ( offset - target ) < distance ) {

				distance = math::abs ( offset - target );
				best = i;

			}

		}

		return best;

	}

	void manager::mouse_down ( float x, float y, double now ) {

		const hit* found = find ( x, y );

		press_action = found ? found->action : ACTION_NONE;
		press_index = found ? found->index : -1;

		if ( press_action == ACTION_FIELD && editing ( ) ) {

			field_focused = true;
			caret_since = now;
			form.name.move ( caret_from ( x ), GetKeyState ( VK_SHIFT ) < 0 );

		} else if ( sheet != SHEET_NONE )
			field_focused = false;

	}

	manager_action manager::mouse_up ( float x, float y, int32_t& index ) {

		const hit* found = find ( x, y );
		manager_action action = found ? found->action : ACTION_NONE;

		index = found ? found->index : -1;

		bool same = action == press_action && index == press_index;

		press_action = ACTION_NONE;
		press_index = -1;

		if ( !same )
			return ACTION_NONE;

		switch ( action ) {

			case ACTION_CANCEL:
				close_sheet ( );
				return ACTION_NONE;

			case ACTION_PRESET:
				form.preset = index;
				return ACTION_NONE;

			case ACTION_MEMORY:
				form.memory = memory_choices [ base::clamp ( index, 0, 2 ) ];
				return ACTION_NONE;

			case ACTION_EJECT:
				form.floppies [ index & 1 ] [ 0 ] = 0;
				return ACTION_NONE;

			case ACTION_BIOS_BUILTIN:
				form.custom_bios = false;
				return ACTION_NONE;

			case ACTION_FIELD:
			case ACTION_SHEET:
				return ACTION_NONE;

			default:
				return action;

		}

	}

	void manager::wheel ( float delta ) {

		if ( sheet == SHEET_NONE )
			scroll = base::clamp ( scroll - delta, 0.0f, scroll_limit );

	}

	manager_action manager::key ( uint32_t code, bool shift, bool control, int32_t& index ) {

		index = 0;

		if ( sheet != SHEET_NONE ) {

			if ( code == VK_ESCAPE ) {

				close_sheet ( );

				return ACTION_NONE;

			}

			if ( code == VK_RETURN ) {

				if ( sheet == SHEET_NEW && name_valid ( ) )
					return ACTION_CREATE;

				if ( sheet == SHEET_SETTINGS && editing ( ) && name_valid ( ) )
					return ACTION_SAVE;

				return ACTION_NONE;

			}

			if ( code == VK_TAB && editing ( ) ) {

				field_focused = !field_focused;
				caret_since = last_paint;

				return ACTION_NONE;

			}

			if ( !field_focused || !editing ( ) )
				return ACTION_NONE;

			text_field& name = form.name;

			caret_since = last_paint;

			if ( code == VK_LEFT )
				name.move ( name.caret - 1, shift );
			else if ( code == VK_RIGHT )
				name.move ( name.caret + 1, shift );
			else if ( code == VK_HOME )
				name.move ( 0, shift );
			else if ( code == VK_END )
				name.move ( name.length, shift );
			else if ( code == VK_BACK )
				name.erase ( false );
			else if ( code == VK_DELETE )
				name.erase ( true );
			else if ( control && code == 'A' ) {

				name.anchor = 0;
				name.caret = name.length;

			}

			return ACTION_NONE;

		}

		if ( control && code == 'N' )
			return ACTION_NEW;

		if ( code == VK_UP || code == VK_DOWN ) {

			index = code == VK_UP ? -1 : 1;

			return ACTION_SELECT;

		}

		if ( code == VK_RETURN )
			return ACTION_START;

		if ( code == VK_DELETE )
			return ACTION_REMOVE;

		return ACTION_NONE;

	}

	void manager::character ( wchar_t value ) {

		if ( !field_focused || !editing ( ) || value < 32 || value == 127 )
			return;

		wchar_t text [ 2 ] = { value, 0 };

		form.name.insert ( text );
		caret_since = last_paint;

	}

	void manager::paste ( const wchar_t* value ) {

		if ( field_focused && editing ( ) && value )
			form.name.insert ( value );

	}

	bool manager::name_valid ( ) const {

		const wchar_t* name = form.name.text;
		int32_t length = form.name.length;
		bool visible = false;

		if ( !length || name [ length - 1 ] == L' ' || name [ length - 1 ] == L'.' )
			return false;

		for ( int32_t i = 0; i < length; i++ ) {

			wchar_t c = name [ i ];

			if ( c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|' )
				return false;

			visible = visible || c != L' ';

		}

		return visible;

	}

	bool manager::animating ( double now ) const {

		if ( hover_action != ACTION_NONE && hover_amount < 0.99f )
			return true;

		if ( math::abs ( sheet_amount - ( sheet != SHEET_NONE ? 1.0f : 0.0f ) ) > 0.01f )
			return true;

		return field_focused || now - toast_since < 5.0;

	}

	void manager::open_sheet ( sheet_kind kind, double now ) {

		sheet = kind;
		sheet_since = now;
		field_focused = kind == SHEET_NEW || ( kind == SHEET_SETTINGS && !subject_running );
		caret_since = now;

	}

	void manager::close_sheet ( ) {

		sheet = SHEET_NONE;
		field_focused = false;

	}

	void manager::toast ( const wchar_t* text, double now ) {

		int32_t length = 0;

		for ( ; text [ length ] && length < 95; length++ )
			toast_text [ length ] = text [ length ];

		toast_text [ length ] = 0;
		toast_since = now;

	}

	bool manager::button ( render::gpu& gpu, const theme& colors, rect area, const wchar_t* label, manager_action action, int32_t style, bool enabled, int32_t index, int32_t icon ) {

		float amount = enabled ? glow ( action, index ) : 0.0f;
		bool pushed = enabled && press_action == action && press_index == index && amount > 0.5f;
		color tint = enabled ? colors.text : colors.faint;
		render::font_role role = render::FONT_BODY;

		if ( style == 0 ) {

			gpu.fill ( area, corner, enabled ? colors.accent : fade ( colors.accent, 0.4f ) );
			tint = enabled ? colors.accent_text : fade ( colors.accent_text, 0.7f );
			role = render::FONT_STRONG;

		} else if ( style == 1 ) {

			gpu.fill ( area, corner, colors.raised );
			gpu.stroke ( area, corner, 1.0f, colors.divider );

		} else if ( style == 3 ) {

			gpu.fill ( area, corner, enabled ? colors.danger : fade ( colors.danger, 0.4f ) );
			tint = render::rgb ( 0xFFFFFF, enabled ? 1.0f : 0.7f );
			role = render::FONT_STRONG;

		} else if ( enabled && amount < 0.5f )
			tint = colors.muted;

		if ( amount > 0.01f )
			gpu.fill ( area, corner, fade ( pushed ? colors.pressed : colors.hover, amount ) );

		if ( icon >= 0 ) {

			float text_width = gpu.measure ( label, role );
			float start = area.x + math::round ( ( area.w - text_width - 22.0f ) * 0.5f );

			gpu.icon ( static_cast < render::icon_kind > ( icon ), start + 7.0f, area.y + area.h * 0.5f, 14.0f, tint );
			gpu.text ( label, role, rect { start + 22.0f, area.y, text_width + 4.0f, area.h }, tint, render::ALIGN_LEFT );

		} else
			gpu.text ( label, role, area, tint, render::ALIGN_CENTER );

		if ( enabled )
			add_hit ( area, action, index );

		return enabled;

	}

	float manager::tool ( render::gpu& gpu, const theme& colors, float x, const wchar_t* label, render::icon_kind icon, manager_action action, bool enabled ) {

		float text_width = gpu.measure ( label, render::FONT_BODY );
		rect area = { x, math::round ( ( toolbar - 30.0f ) * 0.5f ), 10.0f + 16.0f + 7.0f + text_width + 12.0f, 30.0f };
		float amount = enabled ? glow ( action, 0 ) : 0.0f;
		color tint = enabled ? colors.text : colors.faint;

		if ( amount > 0.01f )
			gpu.fill ( area, corner, fade ( press_action == action ? colors.pressed : colors.hover, amount ) );

		gpu.icon ( icon, area.x + 18.0f, area.y + 15.0f, 15.0f, enabled ? ( action == ACTION_START ? colors.accent : colors.muted ) : colors.faint );
		gpu.text ( label, render::FONT_BODY, rect { area.x + 33.0f, area.y, text_width + 4.0f, area.h }, tint, render::ALIGN_LEFT );

		if ( enabled )
			add_hit ( area, action, 0 );

		return area.right ( ) + 2.0f;

	}

	void manager::segmented ( render::gpu& gpu, const theme& colors, rect area, const wchar_t* const* labels, int32_t count, int32_t selected, manager_action action, bool enabled ) {

		gpu.fill ( area, corner, colors.inset );
		gpu.stroke ( area, corner, 1.0f, colors.divider );

		float part = ( area.w - 4.0f ) / static_cast < float > ( count );

		for ( int32_t i = 0; i < count; i++ ) {

			rect segment = { area.x + 2.0f + part * static_cast < float > ( i ), area.y + 2.0f, part, area.h - 4.0f };
			manager_action target = action == ACTION_BIOS_BUILTIN && i == 1 ? ACTION_PICK_BIOS : action;

			if ( i == selected ) {

				gpu.fill ( segment, corner - 1.0f, colors.raised );
				gpu.stroke ( segment, corner - 1.0f, 1.0f, colors.divider );

			} else if ( enabled && glow ( target, i ) > 0.01f )
				gpu.fill ( segment, corner - 1.0f, fade ( colors.hover, glow ( target, i ) ) );

			gpu.text ( labels [ i ], i == selected ? render::FONT_SMALL_STRONG : render::FONT_SMALL, segment, enabled ? ( i == selected ? colors.text : colors.muted ) : colors.faint, render::ALIGN_CENTER );

			if ( enabled )
				add_hit ( segment, target, i );

		}

	}

	void manager::field ( render::gpu& gpu, const theme& colors, rect area, double now ) {

		const text_field& name = form.name;
		bool active = field_focused && editing ( );
		float left = area.x + 10.0f;
		int32_t from = 0;
		int32_t to = 0;
		wchar_t prefix [ text_field::room + 1 ];

		auto offset = [ & ] ( int32_t count ) {

			for ( int32_t j = 0; j < count; j++ )
				prefix [ j ] = name.text [ j ];

			prefix [ count ] = 0;

			return count ? gpu.measure ( prefix, render::FONT_BODY ) : 0.0f;

		};

		field_area = area;
		gpu.fill ( area, corner, colors.inset );
		gpu.stroke ( area, corner, 1.0f, active ? colors.accent : colors.divider );

		if ( active )
			gpu.fill ( rect { area.x + 1.0f, area.bottom ( ) - 2.0f, area.w - 2.0f, 1.0f }, 0.0f, colors.accent );

		if ( active && name.selection ( from, to ) ) {

			float x0 = left + offset ( from );
			float x1 = left + offset ( to );

			gpu.fill ( rect { x0, area.y + 6.0f, x1 - x0, area.h - 12.0f }, 1.0f, fade ( colors.accent, 0.3f ) );

		}

		if ( name.length )
			gpu.text ( name.text, render::FONT_BODY, rect { left, area.y, area.w - 20.0f, area.h }, editing ( ) ? colors.text : colors.muted, render::ALIGN_LEFT );
		else if ( !active )
			gpu.text ( L"Machine name", render::FONT_BODY, rect { left, area.y, area.w - 20.0f, area.h }, colors.faint, render::ALIGN_LEFT );

		if ( active && ( static_cast < int64_t > ( ( now - caret_since ) / 0.53 ) & 1 ) == 0 ) {

			float x = math::round ( left + offset ( name.caret ) ) + 0.5f;

			gpu.line ( x, area.y + 7.0f, x, area.bottom ( ) - 7.0f, 1.0f, colors.text );

		}

		if ( editing ( ) )
			add_hit ( area, ACTION_FIELD );

	}

	void manager::section ( render::gpu& gpu, const theme& colors, float x, float& y, float span, const wchar_t* title ) {

		gpu.text ( title, render::FONT_CAPTION, rect { x, y, span, 16.0f }, colors.muted, render::ALIGN_LEFT );
		gpu.fill ( rect { x, y + 21.0f, span, 1.0f }, 0.0f, colors.divider );
		y += 30.0f;

	}

	void manager::property ( render::gpu& gpu, const theme& colors, float x, float& y, float span, const wchar_t* label, const wchar_t* value, bool quiet ) {

		gpu.text ( label, render::FONT_BODY, rect { x, y, 96.0f, 22.0f }, colors.muted, render::ALIGN_LEFT );
		gpu.text ( value, render::FONT_BODY, rect { x + 100.0f, y, span - 100.0f, 22.0f }, quiet ? colors.faint : colors.text, render::ALIGN_LEFT );
		y += 24.0f;

	}

	void manager::paint ( render::gpu& gpu, const manager_view& state, double now ) {

		theme colors = state.light ? light_theme ( ) : dark_theme ( );
		float dt = static_cast < float > ( base::clamp ( now - last_paint, 0.0, 0.1 ) );

		last_paint = now;
		measurer = &gpu;
		hover_amount = approach ( hover_amount, 1.0f, dt, 18.0f );
		sheet_amount = approach ( sheet_amount, sheet != SHEET_NONE ? 1.0f : 0.0f, dt, 20.0f );
		hit_count = 0;

		if ( !gpu.begin ( colors.background ) )
			return;

		rect pane = { sidebar, toolbar, width - sidebar, height - toolbar - status };

		if ( state.count == 0 )
			paint_empty ( gpu, colors, pane );
		else if ( state.selected >= 0 && state.selected < state.count )
			paint_detail ( gpu, colors, state.machines [ state.selected ], pane );

		paint_sidebar ( gpu, colors, state );
		paint_toolbar ( gpu, colors, state );
		paint_status ( gpu, colors, state, now );

		if ( sheet != SHEET_NONE )
			shown = sheet;

		if ( sheet_amount > 0.01f && shown != SHEET_NONE )
			paint_sheet ( gpu, colors, state, now );

	}

	void manager::paint_toolbar ( render::gpu& gpu, const theme& colors, const manager_view& state ) {

		bool chosen = state.selected >= 0 && state.selected < state.count;
		bool usable = chosen && !state.machines [ state.selected ].problem;
		bool running = usable && state.machines [ state.selected ].running;
		float x = 8.0f;

		gpu.fill ( rect { 0.0f, 0.0f, width, toolbar }, 0.0f, colors.chrome );
		gpu.fill ( rect { 0.0f, toolbar - 1.0f, width, 1.0f }, 0.0f, colors.divider );
		x = tool ( gpu, colors, x, L"New", render::ICON_PLUS, ACTION_NEW, true );
		x = tool ( gpu, colors, x, L"Add", render::ICON_FOLDER, ACTION_ADD, true );
		gpu.fill ( rect { x + 6.0f, 13.0f, 1.0f, toolbar - 26.0f }, 0.0f, colors.divider );
		x += 14.0f;
		x = tool ( gpu, colors, x, running ? L"Show" : L"Start", running ? render::ICON_MONITOR : render::ICON_PLAY, ACTION_START, usable );
		x = tool ( gpu, colors, x, L"Settings", render::ICON_SLIDERS, ACTION_SETTINGS, usable );
		tool ( gpu, colors, x, L"Remove", render::ICON_TRASH, ACTION_REMOVE, chosen );

	}

	void manager::paint_sidebar ( render::gpu& gpu, const theme& colors, const manager_view& state ) {

		rect panel = { 0.0f, toolbar, sidebar, height - toolbar - status };
		rect list = { 0.0f, panel.y + list_header, sidebar, panel.h - list_header };
		base::writer count;

		gpu.fill ( panel, 0.0f, colors.chrome );
		gpu.fill ( rect { sidebar - 1.0f, panel.y, 1.0f, panel.h }, 0.0f, colors.divider );
		count.number ( state.count );
		gpu.text ( L"MACHINES", render::FONT_CAPTION, rect { 16.0f, panel.y + 12.0f, sidebar - 80.0f, 16.0f }, colors.muted, render::ALIGN_LEFT );
		gpu.text ( count.text ( ), render::FONT_CAPTION, rect { sidebar - 64.0f, panel.y + 12.0f, 48.0f, 16.0f }, colors.faint, render::ALIGN_RIGHT );

		scroll_limit = base::max ( 0.0f, static_cast < float > ( state.count ) * row_height + 8.0f - list.h );
		scroll = base::clamp ( scroll, 0.0f, scroll_limit );
		gpu.push_clip ( list );

		for ( int32_t i = 0; i < state.count; i++ ) {

			const machine_card& card = state.machines [ i ];
			rect row = { 6.0f, list.y + static_cast < float > ( i ) * row_height - scroll, sidebar - 13.0f, row_height - 2.0f };
			bool chosen = i == state.selected;

			if ( row.bottom ( ) < list.y || row.y > list.bottom ( ) )
				continue;

			if ( chosen ) {

				gpu.fill ( row, corner, colors.raised );
				gpu.fill ( rect { row.x, row.y + 11.0f, 3.0f, row.h - 22.0f }, 1.5f, colors.accent );

			} else if ( glow ( ACTION_SELECT, i ) > 0.01f )
				gpu.fill ( row, corner, fade ( colors.hover, glow ( ACTION_SELECT, i ) ) );

			gpu.icon ( render::ICON_MONITOR, row.x + 23.0f, row.y + row.h * 0.5f - 1.0f, 17.0f, chosen ? colors.text : colors.muted );

			if ( card.running ) {

				gpu.circle ( row.x + 31.0f, row.y + row.h * 0.5f + 5.0f, 4.0f, chosen ? colors.raised : colors.chrome );
				gpu.circle ( row.x + 31.0f, row.y + row.h * 0.5f + 5.0f, 2.8f, colors.led_green );

			}

			gpu.text ( card.name, chosen ? render::FONT_STRONG : render::FONT_BODY, rect { row.x + 44.0f, row.y + 5.0f, row.w - 54.0f, 18.0f }, colors.text, render::ALIGN_LEFT );

			rect detail = { row.x + 44.0f, row.y + 22.0f, row.w - 54.0f, 16.0f };

			if ( card.problem )
				gpu.text ( L"Can't be opened", render::FONT_SMALL, detail, colors.danger, render::ALIGN_LEFT );
			else {

				base::writer text;

				if ( card.running )
					text.add ( L"Running" );
				else
					text.add ( card.kind ).add ( L"  \x00B7  " ).number ( card.memory ).add ( L" KB" );

				gpu.text ( text.text ( ), render::FONT_SMALL, detail, colors.muted, render::ALIGN_LEFT );

			}

			add_hit ( clip_to ( row, list ), ACTION_SELECT, i );

		}

		gpu.pop_clip ( );

		if ( scroll > 0.5f )
			gpu.fill ( rect { 0.0f, list.y, sidebar - 1.0f, 1.0f }, 0.0f, colors.divider );

	}

	void manager::paint_status ( render::gpu& gpu, const theme& colors, const manager_view& state, double now ) {

		rect bar = { 0.0f, height - status, width, status };
		int32_t running = 0;
		base::writer summary;

		for ( int32_t i = 0; i < state.count; i++ )
			running += state.machines [ i ].running ? 1 : 0;

		gpu.fill ( bar, 0.0f, colors.chrome );
		gpu.fill ( rect { 0.0f, bar.y, width, 1.0f }, 0.0f, colors.divider );
		gpu.circle ( 14.0f, bar.y + status * 0.5f, 3.5f, running ? colors.led_green : colors.led_off );
		summary.number ( state.count ).add ( state.count == 1 ? L" machine" : L" machines" );

		if ( running )
			summary.add ( L"  \x00B7  " ).number ( running ).add ( L" running" );

		float summary_width = gpu.measure ( summary.text ( ), render::FONT_SMALL );

		gpu.text ( summary.text ( ), render::FONT_SMALL, rect { 24.0f, bar.y, summary_width + 4.0f, status }, colors.muted, render::ALIGN_LEFT );

		double age = now - toast_since;

		if ( age < 5.0 && toast_text [ 0 ] ) {

			float alpha = static_cast < float > ( base::min ( base::min ( age / 0.12, 1.0 ), ( 5.0 - age ) / 0.6 ) );
			float x = 24.0f + summary_width + 18.0f;

			gpu.opacity = base::clamp ( alpha, 0.0f, 1.0f );
			gpu.fill ( rect { x - 9.0f, bar.y + 8.0f, 1.0f, status - 16.0f }, 0.0f, colors.divider );
			gpu.text ( toast_text, render::FONT_SMALL, rect { x, bar.y, width * 0.5f, status }, colors.text, render::ALIGN_LEFT );
			gpu.opacity = 1.0f;

		}

		gpu.text ( state.location, render::FONT_SMALL, rect { width * 0.55f, bar.y, width * 0.45f - 14.0f, status }, colors.faint, render::ALIGN_RIGHT );

	}

	void manager::paint_empty ( render::gpu& gpu, const theme& colors, rect pane ) {

		float x = pane.x + math::round ( ( pane.w - 400.0f ) * 0.5f );
		float y = pane.y + math::round ( pane.h * 0.5f - 130.0f );

		gpu.icon ( render::ICON_MONITOR, x + 16.0f, y + 16.0f, 30.0f, colors.faint );
		gpu.text ( L"No machines yet", render::FONT_HEADING, rect { x, y + 44.0f, 400.0f, 26.0f }, colors.text, render::ALIGN_LEFT );
		gpu.text ( L"A machine is an IBM PC/XT with its own floppies, memory and BIOS.", render::FONT_BODY, rect { x, y + 76.0f, 400.0f, 20.0f }, colors.muted, render::ALIGN_LEFT );
		gpu.text ( L"Each one lives in a folder you can copy or back up.", render::FONT_BODY, rect { x, y + 96.0f, 400.0f, 20.0f }, colors.muted, render::ALIGN_LEFT );
		button ( gpu, colors, rect { x, y + 132.0f, 150.0f, 32.0f }, L"New machine", ACTION_NEW, 0, true, 0, render::ICON_PLUS );
		button ( gpu, colors, rect { x + 160.0f, y + 132.0f, 150.0f, 32.0f }, L"Add existing\x2026", ACTION_ADD, 1, true, 0, render::ICON_FOLDER );

		float row = y + 194.0f;

		gpu.text ( L"SHORTCUTS", render::FONT_CAPTION, rect { x, row, 400.0f, 16.0f }, colors.muted, render::ALIGN_LEFT );
		gpu.fill ( rect { x, row + 21.0f, 400.0f, 1.0f }, 0.0f, colors.divider );
		row += 32.0f;

		static const wchar_t* const keys [ 3 ] = { L"Ctrl+N", L"Enter", L"Del" };
		static const wchar_t* const meanings [ 3 ] = { L"New machine", L"Start the selected machine", L"Remove it" };

		for ( int32_t i = 0; i < 3; i++ ) {

			float chip = gpu.measure ( keys [ i ], render::FONT_SMALL ) + 12.0f;

			gpu.stroke ( rect { x + 0.5f, row + 0.5f, chip, 20.0f }, 3.0f, 1.0f, colors.divider );
			gpu.text ( keys [ i ], render::FONT_SMALL, rect { x, row, chip, 21.0f }, colors.text, render::ALIGN_CENTER );
			gpu.text ( meanings [ i ], render::FONT_SMALL, rect { x + 72.0f, row, 320.0f, 21.0f }, colors.muted, render::ALIGN_LEFT );
			row += 28.0f;

		}

	}

	void manager::paint_detail ( render::gpu& gpu, const theme& colors, const machine_card& card, rect pane ) {

		float x = pane.x + 28.0f;
		float inner = pane.w - 56.0f;
		float y = pane.y + 22.0f;

		gpu.text ( card.name, render::FONT_HEADING, rect { x, y, inner, 26.0f }, colors.text, render::ALIGN_LEFT );
		y += 28.0f;

		if ( card.problem ) {

			gpu.text ( L"The machine file can't be opened", render::FONT_SMALL, rect { x, y, inner, 18.0f }, colors.danger, render::ALIGN_LEFT );
			y += 40.0f;
			section ( gpu, colors, x, y, base::min ( inner, 560.0f ), L"PROBLEM" );
			gpu.text ( card.problem, render::FONT_BODY, rect { x, y, base::min ( inner, 560.0f ), 22.0f }, colors.text, render::ALIGN_LEFT );
			y += 40.0f;
			section ( gpu, colors, x, y, base::min ( inner, 560.0f ), L"FILE" );
			gpu.text ( card.folder, render::FONT_BODY, rect { x, y, base::min ( inner, 560.0f ), 22.0f }, colors.muted, render::ALIGN_LEFT );

			return;

		}

		base::writer line;

		gpu.circle ( x + 4.0f, y + 9.0f, 3.5f, card.running ? colors.led_green : colors.led_off );
		line.add ( card.running ? L"Running" : L"Powered off" ).add ( L"  \x00B7  " ).add ( card.kind );
		gpu.text ( line.text ( ), render::FONT_SMALL, rect { x + 14.0f, y, inner - 14.0f, 18.0f }, colors.muted, render::ALIGN_LEFT );
		y += 38.0f;

		float available = pane.bottom ( ) - y - 24.0f;
		bool stacked = inner < 680.0f;
		float side = stacked ? inner : base::clamp ( inner * 0.34f, 250.0f, 320.0f );
		float preview_width = stacked ? inner : inner - side - 28.0f;
		float preview_height = math::round ( preview_width * 0.75f );

		if ( !stacked && preview_height > available ) {

			preview_height = available;
			preview_width = math::round ( preview_height * 4.0f / 3.0f );

		}

		if ( stacked )
			preview_height = base::min ( preview_height, available * 0.45f );

		rect preview = { x, y, preview_width, preview_height };

		if ( card.preview && card.preview_width > 0 ) {

			gpu.fill ( preview, corner, colors.bezel );
			gpu.screen ( card.preview, card.preview_width, card.preview_height, preview.inset ( 8.0f ), false );
			gpu.corners ( preview, corner, colors.background );
			gpu.stroke ( preview, corner, 1.0f, colors.divider );

		} else {

			gpu.fill ( preview, corner, colors.chrome );
			gpu.stroke ( preview, corner, 1.0f, colors.divider );
			gpu.icon ( render::ICON_MONITOR, preview.x + preview.w * 0.5f, preview.y + preview.h * 0.5f - 18.0f, 28.0f, colors.faint );
			gpu.text ( L"No screen yet", render::FONT_STRONG, rect { preview.x, preview.y + preview.h * 0.5f + 6.0f, preview.w, 18.0f }, colors.muted, render::ALIGN_CENTER );
			gpu.text ( L"Its last screen shows up here after it has run", render::FONT_SMALL, rect { preview.x, preview.y + preview.h * 0.5f + 26.0f, preview.w, 18.0f }, colors.faint, render::ALIGN_CENTER );

		}

		float column = stacked ? x : preview.right ( ) + 28.0f;
		float top = stacked ? preview.bottom ( ) + 24.0f : y;
		base::writer memory;

		memory.number ( card.memory ).add ( L" KB" );
		section ( gpu, colors, column, top, side, L"SYSTEM" );
		property ( gpu, colors, column, top, side, L"Machine", card.kind, false );
		property ( gpu, colors, column, top, side, L"Processor", L"Intel 8088, 4.77 MHz", false );
		property ( gpu, colors, column, top, side, L"Memory", memory.text ( ), false );
		property ( gpu, colors, column, top, side, L"Video", L"CGA", false );
		top += 14.0f;
		section ( gpu, colors, column, top, side, L"STORAGE" );
		property ( gpu, colors, column, top, side, L"Floppy A", card.floppies [ 0 ] [ 0 ] ? file_name ( card.floppies [ 0 ] ) : L"Empty", !card.floppies [ 0 ] [ 0 ] );
		property ( gpu, colors, column, top, side, L"Floppy B", card.floppies [ 1 ] [ 0 ] ? file_name ( card.floppies [ 1 ] ) : L"Empty", !card.floppies [ 1 ] [ 0 ] );
		top += 14.0f;
		section ( gpu, colors, column, top, side, L"FIRMWARE" );
		property ( gpu, colors, column, top, side, L"BIOS", card.bios, false );
		top += 14.0f;
		section ( gpu, colors, column, top, side, L"LOCATION" );
		gpu.text ( card.folder, render::FONT_SMALL, rect { column, top, side, 18.0f }, colors.muted, render::ALIGN_LEFT );
		top += 22.0f;

		float link_width = gpu.measure ( L"Open folder", render::FONT_SMALL_STRONG );
		rect link = { column - 4.0f, top, link_width + 8.0f, 20.0f };

		if ( glow ( ACTION_OPEN_FOLDER, 0 ) > 0.01f )
			gpu.fill ( link, 3.0f, fade ( colors.hover, glow ( ACTION_OPEN_FOLDER, 0 ) ) );

		gpu.text ( L"Open folder", render::FONT_SMALL_STRONG, link, colors.accent, render::ALIGN_CENTER );
		add_hit ( link, ACTION_OPEN_FOLDER );

		if ( card.notes [ 0 ] && !stacked ) {

			float notes = preview.bottom ( ) + 24.0f;

			if ( notes + 52.0f < pane.bottom ( ) ) {

				section ( gpu, colors, x, notes, preview.w, L"NOTES" );
				gpu.text ( card.notes, render::FONT_BODY, rect { x, notes, preview.w, 22.0f }, colors.text, render::ALIGN_LEFT );

			}

		}

	}

	float manager::paint_machine_fields ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled ) {

		static const wchar_t* const memory_labels [ 3 ] = { L"256 KB", L"512 KB", L"640 KB" };
		static const wchar_t* const bios_labels [ 2 ] = { L"Coldboot BIOS", L"Custom ROM\x2026" };
		float control_x = x + label_column;
		float control_width = inner - label_column;
		int32_t memory_index = form.memory <= 256 ? 0 : ( form.memory <= 512 ? 1 : 2 );
		color label_tint = enabled ? colors.muted : colors.faint;

		gpu.text ( L"Memory", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, control_width, control_height }, memory_labels, 3, memory_index, ACTION_MEMORY, enabled );
		y += control_height + 10.0f;

		for ( int32_t d = 0; d < 2; d++ ) {

			const wchar_t* path = form.floppies [ d ];
			bool present = path [ 0 ] != 0;
			float buttons = present ? 84.0f + 6.0f + 64.0f : 84.0f;
			rect slot = { control_x, y, control_width - buttons - 6.0f, control_height };

			gpu.text ( d ? L"Floppy B" : L"Floppy A", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
			gpu.fill ( slot, corner, colors.inset );
			gpu.stroke ( slot, corner, 1.0f, colors.divider );
			gpu.text ( present ? file_name ( path ) : L"Empty", render::FONT_BODY, rect { slot.x + 10.0f, slot.y, slot.w - 20.0f, slot.h }, present && enabled ? colors.text : colors.faint, render::ALIGN_LEFT );
			button ( gpu, colors, rect { slot.right ( ) + 6.0f, y, 84.0f, control_height }, L"Browse\x2026", ACTION_PICK_FLOPPY, 1, enabled, d );

			if ( present )
				button ( gpu, colors, rect { slot.right ( ) + 6.0f + 84.0f + 6.0f, y, 64.0f, control_height }, L"Eject", ACTION_EJECT, 2, enabled, d );

			y += control_height + 10.0f;

		}

		gpu.text ( L"BIOS", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, control_width, control_height }, bios_labels, 2, form.custom_bios ? 1 : 0, ACTION_BIOS_BUILTIN, enabled );
		y += control_height + 6.0f;
		gpu.text ( form.custom_bios && form.bios [ 0 ] ? file_name ( form.bios ) : L"Built in, assembled from source at power on", render::FONT_SMALL, rect { control_x, y, control_width, 18.0f }, colors.faint, render::ALIGN_LEFT );

		return y + 26.0f;

	}

	void manager::paint_sheet ( render::gpu& gpu, const theme& colors, const manager_view& state, double now ) {

		bool remove = shown == SHEET_REMOVE;
		float sheet_width = remove ? 460.0f : 540.0f;
		float content = remove ? 60.0f : 54.0f + 180.0f;

		if ( shown == SHEET_NEW )
			content += static_cast < float > ( state.preset_count ) * 46.0f + 2.0f + 18.0f;
		else if ( shown == SHEET_SETTINGS && subject_running )
			content += 44.0f;

		float sheet_height = 56.0f + content + 12.0f + 56.0f;
		rect card = { math::round ( ( width - sheet_width ) * 0.5f ), math::round ( base::max ( ( height - sheet_height ) * 0.5f, 12.0f ) + ( 1.0f - sheet_amount ) * 6.0f ), sheet_width, sheet_height };
		float x = card.x + 20.0f;
		float inner = card.w - 40.0f;
		float y = card.y + 56.0f;
		float footer = card.bottom ( ) - 56.0f;
		float buttons = footer + 13.0f;

		gpu.opacity = sheet_amount;
		gpu.fill ( rect { 0.0f, 0.0f, width, height }, 0.0f, render::rgb ( 0x000000, state.light ? 0.18f : 0.42f ) );
		add_hit ( rect { 0.0f, 0.0f, width, height }, ACTION_SHEET );
		gpu.shadow ( card, corner_large, 18.0f, colors.shadow );
		gpu.fill ( card, corner_large, colors.raised );
		gpu.fill ( rect { card.x + 1.0f, footer, card.w - 2.0f, 56.0f - 1.0f }, corner_large - 1.0f, colors.chrome );
		gpu.fill ( rect { card.x + 1.0f, footer, card.w - 2.0f, 8.0f }, 0.0f, colors.chrome );
		gpu.fill ( rect { card.x + 1.0f, footer, card.w - 2.0f, 1.0f }, 0.0f, colors.divider );
		gpu.stroke ( card, corner_large, 1.0f, colors.divider );
		add_hit ( card, ACTION_SHEET );
		sheet_area = card;

		if ( remove ) {

			base::writer title;

			title.add ( L"Remove \x201C" ).add ( subject ).add ( L"\x201D?" );
			gpu.text ( title.text ( ), render::FONT_TITLE, rect { x, card.y + 18.0f, inner, 24.0f }, colors.text, render::ALIGN_LEFT );

			if ( subject_running ) {

				gpu.text ( L"The machine is running. Close its window first.", render::FONT_BODY, rect { x, y, inner, 20.0f }, colors.muted, render::ALIGN_LEFT );

			} else {

				gpu.text ( L"Removing it from the list keeps its folder and files.", render::FONT_BODY, rect { x, y, inner, 20.0f }, colors.muted, render::ALIGN_LEFT );
				gpu.text ( L"Deleting moves the whole folder to the Recycle Bin.", render::FONT_BODY, rect { x, y + 22.0f, inner, 20.0f }, colors.muted, render::ALIGN_LEFT );

			}

			button ( gpu, colors, rect { card.right ( ) - 20.0f - 80.0f, buttons, 80.0f, control_height }, L"Delete", ACTION_DELETE, 3, !subject_running );
			button ( gpu, colors, rect { card.right ( ) - 20.0f - 80.0f - 8.0f - 136.0f, buttons, 136.0f, control_height }, L"Remove from list", ACTION_FORGET, 1, !subject_running );
			button ( gpu, colors, rect { x, buttons, 80.0f, control_height }, L"Cancel", ACTION_CANCEL, 1, true );
			gpu.opacity = 1.0f;

			return;

		}

		bool enabled = editing ( );

		gpu.text ( shown == SHEET_NEW ? L"New machine" : L"Machine settings", render::FONT_TITLE, rect { x, card.y + 18.0f, inner, 24.0f }, colors.text, render::ALIGN_LEFT );

		if ( shown == SHEET_SETTINGS && subject_running ) {

			rect notice = { x, y, inner, 32.0f };

			gpu.fill ( notice, corner, fade ( colors.accent, 0.1f ) );
			gpu.fill ( rect { notice.x, notice.y, 3.0f, notice.h }, 1.5f, colors.accent );
			gpu.text ( L"The machine is running. Close its window to change these settings.", render::FONT_SMALL, rect { x + 14.0f, y, inner - 24.0f, 32.0f }, colors.text, render::ALIGN_LEFT );
			y += 44.0f;

		}

		gpu.text ( L"Name", render::FONT_BODY, rect { x, y, label_column, control_height }, enabled ? colors.muted : colors.faint, render::ALIGN_LEFT );
		field ( gpu, colors, rect { x + label_column, y, inner - label_column, control_height }, now );
		y += control_height;

		if ( form.name.length && !name_valid ( ) )
			gpu.text ( L"Names can't end with a space or dot, or use \\ / : * ? \" < > |", render::FONT_SMALL, rect { x + label_column, y + 4.0f, inner - label_column, 16.0f }, colors.danger, render::ALIGN_LEFT );

		y += 24.0f;

		if ( shown == SHEET_NEW ) {

			rect group = { x + label_column, y, inner - label_column, static_cast < float > ( state.preset_count ) * 46.0f + 2.0f };

			gpu.text ( L"Type", render::FONT_BODY, rect { x, y, label_column, 46.0f }, colors.muted, render::ALIGN_LEFT );
			gpu.fill ( group, corner, colors.inset );
			gpu.stroke ( group, corner, 1.0f, colors.divider );

			for ( int32_t i = 0; i < state.preset_count; i++ ) {

				const preset_card& item = state.presets [ i ];
				rect option = { group.x + 1.0f, group.y + 1.0f + static_cast < float > ( i ) * 46.0f, group.w - 2.0f, 46.0f };
				bool chosen = form.preset == i;

				if ( i )
					gpu.fill ( rect { option.x, option.y, option.w, 1.0f }, 0.0f, colors.divider );

				if ( glow ( ACTION_PRESET, i ) > 0.01f )
					gpu.fill ( option, corner - 1.0f, fade ( colors.hover, glow ( ACTION_PRESET, i ) ) );

				gpu.ring ( option.x + 18.0f, option.y + 23.0f, 7.0f, 1.2f, chosen ? colors.accent : colors.muted );

				if ( chosen )
					gpu.circle ( option.x + 18.0f, option.y + 23.0f, 3.6f, colors.accent );

				gpu.text ( item.label, render::FONT_BODY, rect { option.x + 36.0f, option.y + 5.0f, option.w - 46.0f, 18.0f }, colors.text, render::ALIGN_LEFT );
				gpu.text ( item.detail, render::FONT_SMALL, rect { option.x + 36.0f, option.y + 23.0f, option.w - 46.0f, 16.0f }, colors.muted, render::ALIGN_LEFT );
				add_hit ( option, ACTION_PRESET, i );

			}

			y = group.bottom ( ) + 18.0f;

		}

		paint_machine_fields ( gpu, colors, x, y, inner, enabled );

		float primary = shown == SHEET_NEW ? 88.0f : 80.0f;

		if ( shown == SHEET_NEW ) {

			base::writer place;

			place.add ( L"Saved in " ).add ( state.location );
			gpu.text ( place.text ( ), render::FONT_SMALL, rect { x, buttons, inner - primary - 8.0f - 80.0f - 16.0f, control_height }, colors.faint, render::ALIGN_LEFT );
			button ( gpu, colors, rect { card.right ( ) - 20.0f - primary, buttons, primary, control_height }, L"Create", ACTION_CREATE, 0, name_valid ( ) );

		} else
			button ( gpu, colors, rect { card.right ( ) - 20.0f - primary, buttons, primary, control_height }, L"Save", ACTION_SAVE, 0, enabled && name_valid ( ) );

		button ( gpu, colors, rect { card.right ( ) - 20.0f - primary - 8.0f - 80.0f, buttons, 80.0f, control_height }, L"Cancel", ACTION_CANCEL, 1, true );
		gpu.opacity = 1.0f;

	}

}