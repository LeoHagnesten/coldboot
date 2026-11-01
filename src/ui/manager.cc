#include "ui/manager.hh"
#include "base/math.hh"
#include <windows.h>

namespace ui {

	using render::rect;
	using render::color;
	using render::fade;

	static constexpr float row_height = 58.0f;
	static constexpr float row_gap = 4.0f;
	static constexpr float list_top = 64.0f;
	static constexpr float list_bottom = 56.0f;

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

			if ( forward && caret < length )
				to = caret + 1, from = caret;
			else if ( !forward && caret > 0 )
				from = caret - 1, to = caret;
			else
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
		sidebar = base::clamp ( width * 0.26f, 248.0f, 320.0f );

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

		float target = x - ( field_area.x + 12.0f );
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

		return field_focused || now - toast_since < 2.6;

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

	bool manager::button ( render::gpu& gpu, const theme& colors, rect area, const wchar_t* label, manager_action action, int32_t style, bool enabled, int32_t index ) {

		float amount = enabled ? glow ( action, index ) : 0.0f;
		bool pushed = enabled && press_action == action && press_index == index && amount > 0.5f;
		color label_tint = colors.text;
		render::font_role role = render::FONT_BODY;

		if ( style == 0 ) {

			gpu.fill ( area, 8.0f, enabled ? colors.accent : fade ( colors.accent, 0.35f ) );

			if ( amount > 0.01f )
				gpu.fill ( area, 8.0f, fade ( pushed ? colors.pressed : colors.hover, amount ) );

			label_tint = enabled ? colors.accent_text : fade ( colors.accent_text, 0.7f );
			role = render::FONT_STRONG;

		} else if ( style == 1 ) {

			gpu.fill ( area, 8.0f, colors.raised );
			gpu.stroke ( area, 8.0f, 1.0f, colors.divider );

			if ( amount > 0.01f )
				gpu.fill ( area, 8.0f, fade ( pushed ? colors.pressed : colors.hover, amount ) );

			label_tint = enabled ? colors.text : colors.faint;

		} else if ( style == 2 ) {

			if ( amount > 0.01f )
				gpu.fill ( area, 8.0f, fade ( pushed ? colors.pressed : colors.hover, amount ) );

			label_tint = enabled ? ( amount > 0.5f ? colors.text : colors.muted ) : colors.faint;

		} else {

			gpu.fill ( area, 8.0f, fade ( colors.danger, enabled ? 0.14f + amount * 0.08f : 0.06f ) );
			label_tint = enabled ? colors.danger : fade ( colors.danger, 0.45f );
			role = render::FONT_STRONG;

		}

		gpu.text ( label, role, area, label_tint, render::ALIGN_CENTER );

		if ( enabled )
			add_hit ( area, action, index );

		return enabled;

	}

	void manager::badge ( render::gpu& gpu, const theme& colors, rect area, const wchar_t* mark, bool large ) {

		gpu.fill ( area, large ? 18.0f : 9.0f, fade ( colors.accent, 0.15f ) );
		gpu.stroke ( area, large ? 18.0f : 9.0f, 1.0f, fade ( colors.accent, 0.28f ) );
		gpu.text ( mark, large ? render::FONT_HERO : render::FONT_SMALL_STRONG, area, colors.accent, render::ALIGN_CENTER );

	}

	void manager::segmented ( render::gpu& gpu, const theme& colors, rect area, const wchar_t* const* labels, int32_t count, int32_t selected, manager_action action, bool enabled ) {

		gpu.fill ( area, 8.0f, colors.inset );

		float part = ( area.w - 4.0f ) / static_cast < float > ( count );

		for ( int32_t i = 0; i < count; i++ ) {

			rect segment = { area.x + 2.0f + part * static_cast < float > ( i ), area.y + 2.0f, part, area.h - 4.0f };
			manager_action target = action == ACTION_BIOS_BUILTIN && i == 1 ? ACTION_PICK_BIOS : action;

			if ( i == selected ) {

				gpu.fill ( segment, 6.0f, colors.raised );
				gpu.stroke ( segment, 6.0f, 1.0f, colors.divider );

			} else if ( enabled && glow ( target, i ) > 0.01f )
				gpu.fill ( segment, 6.0f, fade ( colors.hover, glow ( target, i ) ) );

			gpu.text ( labels [ i ], i == selected ? render::FONT_SMALL_STRONG : render::FONT_SMALL, segment, enabled ? ( i == selected ? colors.text : colors.muted ) : colors.faint, render::ALIGN_CENTER );

			if ( enabled )
				add_hit ( segment, target, i );

		}

	}

	void manager::field ( render::gpu& gpu, const theme& colors, rect area, double now ) {

		const text_field& name = form.name;
		bool active = field_focused && editing ( );
		float left = area.x + 12.0f;
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
		gpu.fill ( area, 8.0f, colors.inset );
		gpu.stroke ( area, 8.0f, active ? 1.5f : 1.0f, active ? colors.accent : colors.divider );

		if ( active && name.selection ( from, to ) ) {

			float x0 = left + offset ( from );
			float x1 = left + offset ( to );

			gpu.fill ( rect { x0, area.y + 8.0f, x1 - x0, area.h - 16.0f }, 2.0f, fade ( colors.accent, 0.3f ) );

		}

		if ( name.length )
			gpu.text ( name.text, render::FONT_BODY, rect { left, area.y, area.w - 24.0f, area.h }, editing ( ) ? colors.text : colors.muted, render::ALIGN_LEFT );
		else if ( !active )
			gpu.text ( L"Give it a name", render::FONT_BODY, rect { left, area.y, area.w - 24.0f, area.h }, colors.faint, render::ALIGN_LEFT );

		if ( active && ( static_cast < int64_t > ( ( now - caret_since ) / 0.53 ) & 1 ) == 0 ) {

			float x = math::round ( left + offset ( name.caret ) ) + 0.5f;

			gpu.line ( x, area.y + 9.0f, x, area.bottom ( ) - 9.0f, 1.2f, colors.text );

		}

		if ( editing ( ) )
			add_hit ( area, ACTION_FIELD );

	}

	void manager::paint ( render::gpu& gpu, const manager_view& state, double now ) {

		theme colors = state.light ? light_theme ( ) : dark_theme ( );
		float dt = static_cast < float > ( base::clamp ( now - last_paint, 0.0, 0.1 ) );

		last_paint = now;
		measurer = &gpu;
		hover_amount = approach ( hover_amount, 1.0f, dt, 16.0f );
		sheet_amount = approach ( sheet_amount, sheet != SHEET_NONE ? 1.0f : 0.0f, dt, 18.0f );
		hit_count = 0;

		if ( !gpu.begin ( colors.background ) )
			return;

		rect pane = { sidebar, 0.0f, width - sidebar, height };

		if ( state.count == 0 )
			paint_empty ( gpu, colors, pane );
		else if ( state.selected >= 0 && state.selected < state.count )
			paint_detail ( gpu, colors, state.machines [ state.selected ], pane );

		paint_sidebar ( gpu, colors, state );

		if ( sheet != SHEET_NONE )
			shown = sheet;

		if ( sheet_amount > 0.01f && shown != SHEET_NONE )
			paint_sheet ( gpu, colors, state, now );

		double age = now - toast_since;

		if ( age < 2.4 && toast_text [ 0 ] ) {

			float alpha = static_cast < float > ( base::min ( base::min ( age / 0.15, 1.0 ), base::min ( ( 2.4 - age ) / 0.4, 1.0 ) ) );
			float text_width = gpu.measure ( toast_text, render::FONT_BODY );
			rect pill = { math::round ( sidebar + ( width - sidebar - text_width - 36.0f ) * 0.5f ), height - 64.0f, text_width + 36.0f, 34.0f };

			gpu.opacity = alpha;
			gpu.shadow ( pill, 17.0f, 10.0f, colors.shadow );
			gpu.fill ( pill, 17.0f, colors.raised );
			gpu.stroke ( pill, 17.0f, 1.0f, colors.divider );
			gpu.text ( toast_text, render::FONT_BODY, pill, colors.text, render::ALIGN_CENTER );
			gpu.opacity = 1.0f;

		}

	}

	void manager::paint_sidebar ( render::gpu& gpu, const theme& colors, const manager_view& state ) {

		rect list = { 0.0f, list_top, sidebar, height - list_top - list_bottom };

		gpu.fill ( rect { 0.0f, 0.0f, sidebar, height }, 0.0f, colors.chrome );
		gpu.fill ( rect { sidebar - 1.0f, 0.0f, 1.0f, height }, 0.0f, colors.divider );
		gpu.text ( L"Machines", render::FONT_TITLE, rect { 20.0f, 16.0f, sidebar - 120.0f, 32.0f }, colors.text, render::ALIGN_LEFT );
		button ( gpu, colors, rect { sidebar - 16.0f - 76.0f, 16.0f, 76.0f, 32.0f }, L"New", ACTION_NEW, 0, true );

		scroll_limit = base::max ( 0.0f, static_cast < float > ( state.count ) * ( row_height + row_gap ) + 8.0f - list.h );
		scroll = base::clamp ( scroll, 0.0f, scroll_limit );
		gpu.push_clip ( list );

		for ( int32_t i = 0; i < state.count; i++ ) {

			const machine_card& card = state.machines [ i ];
			rect row = { 10.0f, list.y + 4.0f + static_cast < float > ( i ) * ( row_height + row_gap ) - scroll, sidebar - 20.0f, row_height };

			if ( row.bottom ( ) < list.y || row.y > list.bottom ( ) )
				continue;

			if ( i == state.selected ) {

				gpu.fill ( row, 10.0f, colors.raised );
				gpu.stroke ( row, 10.0f, 1.0f, colors.divider );

			} else if ( glow ( ACTION_SELECT, i ) > 0.01f )
				gpu.fill ( row, 10.0f, fade ( colors.hover, glow ( ACTION_SELECT, i ) ) );

			badge ( gpu, colors, rect { row.x + 10.0f, row.y + 11.0f, 36.0f, 36.0f }, card.mark, false );
			gpu.text ( card.name, render::FONT_STRONG, rect { row.x + 58.0f, row.y + 9.0f, row.w - 68.0f, 20.0f }, colors.text, render::ALIGN_LEFT );

			rect detail = { row.x + 58.0f, row.y + 29.0f, row.w - 68.0f, 18.0f };

			if ( card.problem )
				gpu.text ( L"Can't be opened", render::FONT_SMALL, detail, colors.danger, render::ALIGN_LEFT );
			else if ( card.running ) {

				gpu.circle ( detail.x + 4.0f, detail.y + 9.0f, 3.5f, colors.led_green );
				gpu.text ( L"Running", render::FONT_SMALL, rect { detail.x + 13.0f, detail.y, detail.w - 13.0f, detail.h }, colors.muted, render::ALIGN_LEFT );

			} else {

				base::writer text;

				text.add ( card.kind ).add ( L"  \x00B7  " ).number ( card.memory ).add ( L" KB" );
				gpu.text ( text.text ( ), render::FONT_SMALL, detail, colors.muted, render::ALIGN_LEFT );

			}

			add_hit ( clip_to ( row, list ), ACTION_SELECT, i );

		}

		gpu.pop_clip ( );

		if ( scroll > 0.5f )
			gpu.fill ( rect { 0.0f, list.y, sidebar - 1.0f, 1.0f }, 0.0f, colors.divider );

		gpu.fill ( rect { 0.0f, height - list_bottom, sidebar - 1.0f, 1.0f }, 0.0f, colors.divider );
		button ( gpu, colors, rect { 10.0f, height - list_bottom + 11.0f, sidebar - 20.0f, 34.0f }, L"Add an existing machine\x2026", ACTION_ADD, 2, true );

	}

	void manager::paint_empty ( render::gpu& gpu, const theme& colors, rect pane ) {

		float middle = pane.x + pane.w * 0.5f;
		float top = math::round ( height * 0.5f - 110.0f );

		badge ( gpu, colors, rect { middle - 36.0f, top, 72.0f, 72.0f }, L"XT", true );
		gpu.text ( L"No machines yet", render::FONT_HERO, rect { pane.x, top + 92.0f, pane.w, 32.0f }, colors.text, render::ALIGN_CENTER );
		gpu.text ( L"Make an IBM PC/XT to boot DOS, run software from the early 80s,", render::FONT_BODY, rect { pane.x, top + 132.0f, pane.w, 20.0f }, colors.muted, render::ALIGN_CENTER );
		gpu.text ( L"or try boot sector code you wrote yourself.", render::FONT_BODY, rect { pane.x, top + 152.0f, pane.w, 20.0f }, colors.muted, render::ALIGN_CENTER );
		button ( gpu, colors, rect { middle - 146.0f, top + 194.0f, 140.0f, 36.0f }, L"New machine", ACTION_NEW, 0, true );
		button ( gpu, colors, rect { middle + 6.0f, top + 194.0f, 140.0f, 36.0f }, L"Add existing\x2026", ACTION_ADD, 1, true );

	}

	void manager::paint_detail ( render::gpu& gpu, const theme& colors, const machine_card& card, rect pane ) {

		float x = pane.x + 32.0f;
		float inner = pane.w - 64.0f;
		float actions = card.problem ? 96.0f : 308.0f;
		float right = x + inner;

		gpu.text ( card.name, render::FONT_HERO, rect { x, 26.0f, inner - actions - 16.0f, 32.0f }, colors.text, render::ALIGN_LEFT );

		rect status = { x, 60.0f, inner - actions, 18.0f };

		if ( card.problem ) {

			gpu.text ( L"The machine file can't be opened", render::FONT_SMALL, status, colors.danger, render::ALIGN_LEFT );
			button ( gpu, colors, rect { right - 96.0f, 28.0f, 96.0f, 34.0f }, L"Remove", ACTION_REMOVE, 1, true );

			rect card_area = { x, 104.0f, base::min ( inner, 620.0f ), 112.0f };

			gpu.fill ( card_area, 12.0f, colors.chrome );
			gpu.stroke ( card_area, 12.0f, 1.0f, colors.divider );
			gpu.text ( L"Coldboot couldn't read this machine", render::FONT_STRONG, rect { card_area.x + 20.0f, card_area.y + 18.0f, card_area.w - 40.0f, 20.0f }, colors.text, render::ALIGN_LEFT );
			gpu.text ( card.problem, render::FONT_BODY, rect { card_area.x + 20.0f, card_area.y + 44.0f, card_area.w - 40.0f, 20.0f }, colors.muted, render::ALIGN_LEFT );
			gpu.text ( card.folder, render::FONT_SMALL, rect { card_area.x + 20.0f, card_area.y + 72.0f, card_area.w - 40.0f, 18.0f }, colors.faint, render::ALIGN_LEFT );

			return;

		}

		gpu.circle ( status.x + 4.0f, status.y + 9.0f, 3.5f, card.running ? colors.led_green : colors.led_off );

		base::writer line;

		line.add ( card.running ? L"Running" : L"Powered off" ).add ( L"  \x00B7  " ).add ( card.kind );
		gpu.text ( line.text ( ), render::FONT_SMALL, rect { status.x + 14.0f, status.y, status.w - 14.0f, status.h }, colors.muted, render::ALIGN_LEFT );
		button ( gpu, colors, rect { right - 120.0f, 28.0f, 120.0f, 34.0f }, card.running ? L"Show window" : L"Start", ACTION_START, 0, true );
		button ( gpu, colors, rect { right - 120.0f - 8.0f - 100.0f, 28.0f, 100.0f, 34.0f }, L"Settings", ACTION_SETTINGS, 1, true );
		button ( gpu, colors, rect { right - 120.0f - 8.0f - 100.0f - 8.0f - 80.0f, 28.0f, 80.0f, 34.0f }, L"Remove", ACTION_REMOVE, 2, true );

		float top = 104.0f;
		float available = height - top - 32.0f;
		bool stacked = inner < 720.0f;
		float details_width = stacked ? inner : base::clamp ( inner * 0.36f, 260.0f, 340.0f );
		float preview_width = stacked ? inner : inner - details_width - 20.0f;
		float preview_height = math::round ( preview_width * 0.75f );

		if ( !stacked && preview_height > available ) {

			preview_height = available;
			preview_width = math::round ( preview_height * 4.0f / 3.0f );

		}

		if ( stacked )
			preview_height = base::min ( preview_height, available * 0.5f );

		rect preview = { x, top, preview_width, preview_height };

		if ( card.preview && card.preview_width > 0 ) {

			gpu.fill ( preview, 12.0f, colors.bezel );
			gpu.screen ( card.preview, card.preview_width, card.preview_height, preview.inset ( 10.0f ), false );
			gpu.corners ( preview, 12.0f, colors.background );
			gpu.stroke ( preview, 12.0f, 1.0f, colors.divider );

		} else {

			gpu.fill ( preview, 12.0f, colors.chrome );
			gpu.stroke ( preview, 12.0f, 1.0f, colors.divider );
			badge ( gpu, colors, rect { preview.x + preview.w * 0.5f - 32.0f, preview.y + preview.h * 0.5f - 52.0f, 64.0f, 64.0f }, card.mark, true );
			gpu.text ( L"The screen shows up here after the first run", render::FONT_SMALL, rect { preview.x, preview.y + preview.h * 0.5f + 26.0f, preview.w, 18.0f }, colors.faint, render::ALIGN_CENTER );

		}

		rect details = stacked ? rect { x, preview.bottom ( ) + 20.0f, inner, 0.0f } : rect { preview.right ( ) + 20.0f, top, details_width, 0.0f };
		float y = details.y + 18.0f;
		float label_width = 84.0f;
		base::writer memory;

		memory.number ( card.memory ).add ( L" KB" );

		const wchar_t* labels [ 7 ] = { L"Machine", L"Processor", L"Memory", L"Video", L"Floppy A", L"Floppy B", L"BIOS" };
		const wchar_t* values [ 7 ] = { card.kind, L"Intel 8088 at 4.77 MHz", memory.text ( ), L"CGA", card.floppies [ 0 ] [ 0 ] ? file_name ( card.floppies [ 0 ] ) : L"Empty", card.floppies [ 1 ] [ 0 ] ? file_name ( card.floppies [ 1 ] ) : L"Empty", card.bios };

		details.h = 18.0f + 7.0f * 30.0f + 14.0f + 16.0f + 20.0f + 20.0f + 22.0f + 18.0f;
		gpu.fill ( details, 12.0f, colors.chrome );
		gpu.stroke ( details, 12.0f, 1.0f, colors.divider );

		for ( int32_t i = 0; i < 7; i++ ) {

			bool empty = ( i == 4 || i == 5 ) && !card.floppies [ i - 4 ] [ 0 ];

			gpu.text ( labels [ i ], render::FONT_SMALL, rect { details.x + 18.0f, y, label_width, 22.0f }, colors.muted, render::ALIGN_LEFT );
			gpu.text ( values [ i ], render::FONT_BODY, rect { details.x + 18.0f + label_width, y, details.w - 36.0f - label_width, 22.0f }, empty ? colors.faint : colors.text, render::ALIGN_LEFT );
			y += 30.0f;

		}

		y += 14.0f;
		gpu.fill ( rect { details.x + 18.0f, y, details.w - 36.0f, 1.0f }, 0.0f, colors.divider );
		y += 16.0f;
		gpu.text ( L"Location", render::FONT_SMALL, rect { details.x + 18.0f, y, details.w - 36.0f, 20.0f }, colors.muted, render::ALIGN_LEFT );
		y += 20.0f;
		gpu.text ( card.folder, render::FONT_SMALL, rect { details.x + 18.0f, y, details.w - 36.0f, 18.0f }, colors.text, render::ALIGN_LEFT );

		float link_width = gpu.measure ( L"Open folder", render::FONT_SMALL_STRONG ) + 16.0f;
		rect link = { details.x + 10.0f, y + 20.0f, link_width, 22.0f };

		if ( glow ( ACTION_OPEN_FOLDER, 0 ) > 0.01f )
			gpu.fill ( link, 6.0f, fade ( colors.hover, glow ( ACTION_OPEN_FOLDER, 0 ) ) );

		gpu.text ( L"Open folder", render::FONT_SMALL_STRONG, link, colors.accent, render::ALIGN_CENTER );
		add_hit ( link, ACTION_OPEN_FOLDER );

		if ( card.notes [ 0 ] && !stacked ) {

			gpu.text ( L"Notes", render::FONT_SMALL, rect { x, preview.bottom ( ) + 16.0f, preview.w, 18.0f }, colors.muted, render::ALIGN_LEFT );
			gpu.text ( card.notes, render::FONT_BODY, rect { x, preview.bottom ( ) + 36.0f, preview.w, 20.0f }, colors.text, render::ALIGN_LEFT );

		}

	}

	float manager::paint_machine_fields ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled ) {

		static const wchar_t* const memory_labels [ 3 ] = { L"256 KB", L"512 KB", L"640 KB" };
		static const wchar_t* const bios_labels [ 2 ] = { L"Coldboot", L"Custom\x2026" };
		float control_x = x + 112.0f;
		float control_width = inner - 112.0f;
		int32_t memory_index = form.memory <= 256 ? 0 : ( form.memory <= 512 ? 1 : 2 );

		gpu.text ( L"Memory", render::FONT_BODY, rect { x, y, 110.0f, 34.0f }, enabled ? colors.text : colors.muted, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, control_width, 34.0f }, memory_labels, 3, memory_index, ACTION_MEMORY, enabled );
		y += 44.0f;

		for ( int32_t d = 0; d < 2; d++ ) {

			const wchar_t* path = form.floppies [ d ];
			bool present = path [ 0 ] != 0;
			float buttons = present ? 80.0f + 8.0f + 72.0f : 80.0f;
			rect slot = { control_x, y, control_width - buttons - 8.0f, 34.0f };

			gpu.text ( d ? L"Floppy B" : L"Floppy A", render::FONT_BODY, rect { x, y, 110.0f, 34.0f }, enabled ? colors.text : colors.muted, render::ALIGN_LEFT );
			gpu.fill ( slot, 8.0f, colors.inset );
			gpu.text ( present ? file_name ( path ) : L"Empty", render::FONT_BODY, rect { slot.x + 12.0f, slot.y, slot.w - 24.0f, slot.h }, present && enabled ? colors.text : colors.faint, render::ALIGN_LEFT );
			button ( gpu, colors, rect { slot.right ( ) + 8.0f, y, 80.0f, 34.0f }, L"Choose\x2026", ACTION_PICK_FLOPPY, 1, enabled, d );

			if ( present )
				button ( gpu, colors, rect { slot.right ( ) + 8.0f + 80.0f + 8.0f, y, 72.0f, 34.0f }, L"Eject", ACTION_EJECT, 2, enabled, d );

			y += 44.0f;

		}

		gpu.text ( L"BIOS", render::FONT_BODY, rect { x, y, 110.0f, 34.0f }, enabled ? colors.text : colors.muted, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, control_width, 34.0f }, bios_labels, 2, form.custom_bios ? 1 : 0, ACTION_BIOS_BUILTIN, enabled );
		y += 40.0f;
		gpu.text ( form.custom_bios && form.bios [ 0 ] ? file_name ( form.bios ) : L"Built in, assembled from source at power on", render::FONT_SMALL, rect { control_x, y, control_width, 18.0f }, colors.muted, render::ALIGN_LEFT );

		return y + 26.0f;

	}

	void manager::paint_sheet ( render::gpu& gpu, const theme& colors, const manager_view& state, double now ) {

		float sheet_width = shown == SHEET_REMOVE ? 480.0f : 560.0f;
		float sheet_height = 64.0f + 90.0f + 198.0f + 16.0f + 60.0f;

		if ( shown == SHEET_REMOVE )
			sheet_height = 196.0f;
		else if ( shown == SHEET_NEW )
			sheet_height += 34.0f + static_cast < float > ( state.preset_count ) * 64.0f;
		else if ( subject_running )
			sheet_height += 48.0f;
		rect card = { math::round ( ( width - sheet_width ) * 0.5f ), math::round ( base::max ( ( height - sheet_height ) * 0.5f, 16.0f ) + ( 1.0f - sheet_amount ) * 10.0f ), sheet_width, sheet_height };
		float x = card.x + 28.0f;
		float inner = card.w - 56.0f;
		float y = card.y + 24.0f;

		gpu.opacity = sheet_amount;
		gpu.fill ( rect { 0.0f, 0.0f, width, height }, 0.0f, render::rgb ( 0x000000, state.light ? 0.22f : 0.45f ) );
		add_hit ( rect { 0.0f, 0.0f, width, height }, ACTION_SHEET );
		gpu.shadow ( card, 16.0f, 24.0f, colors.shadow );
		gpu.fill ( card, 16.0f, colors.raised );
		gpu.stroke ( card, 16.0f, 1.0f, colors.divider );
		add_hit ( card, ACTION_SHEET );
		sheet_area = card;

		float footer = card.bottom ( ) - 24.0f - 36.0f;

		if ( shown == SHEET_REMOVE ) {

			base::writer title;

			title.add ( L"Remove \x201C" ).add ( subject ).add ( L"\x201D?" );
			gpu.text ( title.text ( ), render::FONT_TITLE, rect { x, y, inner, 26.0f }, colors.text, render::ALIGN_LEFT );
			y += 42.0f;

			if ( subject_running ) {

				gpu.text ( L"Close the machine's window first.", render::FONT_BODY, rect { x, y, inner, 20.0f }, colors.muted, render::ALIGN_LEFT );

			} else {

				gpu.text ( L"Removing it from the list keeps its folder and files.", render::FONT_BODY, rect { x, y, inner, 20.0f }, colors.muted, render::ALIGN_LEFT );
				gpu.text ( L"Deleting moves the whole folder to the Recycle Bin.", render::FONT_BODY, rect { x, y + 22.0f, inner, 20.0f }, colors.muted, render::ALIGN_LEFT );

			}

			button ( gpu, colors, rect { card.right ( ) - 28.0f - 96.0f, footer, 96.0f, 36.0f }, L"Delete", ACTION_DELETE, 3, !subject_running );
			button ( gpu, colors, rect { card.right ( ) - 28.0f - 96.0f - 8.0f - 150.0f, footer, 150.0f, 36.0f }, L"Remove from list", ACTION_FORGET, 1, !subject_running );
			button ( gpu, colors, rect { x - 8.0f, footer, 88.0f, 36.0f }, L"Cancel", ACTION_CANCEL, 2, true );
			gpu.opacity = 1.0f;

			return;

		}

		bool enabled = editing ( );

		gpu.text ( shown == SHEET_NEW ? L"New machine" : L"Machine settings", render::FONT_TITLE, rect { x, y, inner, 26.0f }, colors.text, render::ALIGN_LEFT );
		y += 40.0f;

		if ( shown == SHEET_SETTINGS && subject_running ) {

			gpu.fill ( rect { x, y, inner, 36.0f }, 8.0f, fade ( colors.accent, 0.1f ) );
			gpu.text ( L"Close the machine's window to change its settings.", render::FONT_BODY, rect { x + 12.0f, y, inner - 24.0f, 36.0f }, colors.text, render::ALIGN_LEFT );
			y += 48.0f;

		}

		gpu.text ( L"Name", render::FONT_SMALL_STRONG, rect { x, y, inner, 18.0f }, colors.muted, render::ALIGN_LEFT );
		y += 22.0f;
		field ( gpu, colors, rect { x, y, inner, 38.0f }, now );
		y += 38.0f;

		if ( form.name.length && !name_valid ( ) )
			gpu.text ( L"Names can't end with a space or dot, or use \\ / : * ? \" < > |", render::FONT_SMALL, rect { x, y + 4.0f, inner, 18.0f }, colors.danger, render::ALIGN_LEFT );

		y += 30.0f;

		if ( shown == SHEET_NEW ) {

			gpu.text ( L"Start from", render::FONT_SMALL_STRONG, rect { x, y, inner, 18.0f }, colors.muted, render::ALIGN_LEFT );
			y += 24.0f;

			for ( int32_t i = 0; i < state.preset_count; i++ ) {

				const preset_card& item = state.presets [ i ];
				rect option = { x, y, inner, 56.0f };
				bool chosen = form.preset == i;

				gpu.fill ( option, 10.0f, chosen ? fade ( colors.accent, 0.08f ) : colors.inset );

				if ( !chosen && glow ( ACTION_PRESET, i ) > 0.01f )
					gpu.fill ( option, 10.0f, fade ( colors.hover, glow ( ACTION_PRESET, i ) ) );

				gpu.stroke ( option, 10.0f, chosen ? 1.5f : 1.0f, chosen ? colors.accent : colors.divider );
				badge ( gpu, colors, rect { option.x + 10.0f, option.y + 10.0f, 36.0f, 36.0f }, item.mark, false );
				gpu.text ( item.label, render::FONT_STRONG, rect { option.x + 58.0f, option.y + 9.0f, option.w - 70.0f, 20.0f }, colors.text, render::ALIGN_LEFT );
				gpu.text ( item.detail, render::FONT_SMALL, rect { option.x + 58.0f, option.y + 28.0f, option.w - 70.0f, 18.0f }, colors.muted, render::ALIGN_LEFT );
				add_hit ( option, ACTION_PRESET, i );
				y += 64.0f;

			}

			y += 10.0f;

		}

		paint_machine_fields ( gpu, colors, x, y, inner, enabled );

		if ( shown == SHEET_NEW ) {

			base::writer place;

			place.add ( L"Saved in " ).add ( state.location );
			gpu.text ( place.text ( ), render::FONT_SMALL, rect { x, footer, inner - 240.0f, 36.0f }, colors.faint, render::ALIGN_LEFT );
			button ( gpu, colors, rect { card.right ( ) - 28.0f - 104.0f, footer, 104.0f, 36.0f }, L"Create", ACTION_CREATE, 0, name_valid ( ) );

		} else
			button ( gpu, colors, rect { card.right ( ) - 28.0f - 104.0f, footer, 104.0f, 36.0f }, L"Save", ACTION_SAVE, 0, enabled && name_valid ( ) );

		button ( gpu, colors, rect { card.right ( ) - 28.0f - 104.0f - 8.0f - 96.0f, footer, 96.0f, 36.0f }, L"Cancel", ACTION_CANCEL, 1, true );
		gpu.opacity = 1.0f;

	}

}