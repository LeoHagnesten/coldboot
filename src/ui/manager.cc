#include "ui/manager.hh"
#include "base/math.hh"
#include "vm/config.hh"
#include "vm/unattend.hh"
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

	static void memory_text ( base::writer& out, uint32_t megabytes ) {

		if ( megabytes < 1024 )
			out.number ( megabytes ).add ( L" MB" );
		else if ( megabytes % 1024 )
			out.fixed ( megabytes / 1024.0, 1 ).add ( L" GB" );
		else
			out.number ( megabytes / 1024 ).add ( L" GB" );

	}

	static uint32_t snap ( uint32_t value, uint32_t step ) {

		return ( value + step / 2 ) / step * step;

	}

	static float disk_position ( uint32_t gigabytes ) {

		float value = static_cast < float > ( base::clamp ( gigabytes, 4u, 2048u ) ) / 4.0f;
		float octaves = 0.0f;

		while ( value >= 2.0f ) {

			value *= 0.5f;
			octaves += 1.0f;

		}

		return base::clamp ( ( octaves + value - 1.0f ) / 9.0f, 0.0f, 1.0f );

	}

	static uint32_t disk_from ( float position ) {

		float scaled = base::clamp ( position, 0.0f, 1.0f ) * 9.0f;
		int32_t octave = base::min ( static_cast < int32_t > ( scaled ), 8 );
		float value = 4.0f * static_cast < float > ( 1 << octave ) * ( 1.0f + scaled - static_cast < float > ( octave ) );
		uint32_t gigabytes = static_cast < uint32_t > ( value + 0.5f );
		uint32_t step = gigabytes < 32 ? 1u : ( gigabytes < 128 ? 4u : ( gigabytes < 512 ? 16u : 64u ) );

		return base::clamp ( snap ( gigabytes, step ), 4u, 2048u );

	}

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

		if ( dragging >= 0 ) {

			set_slider ( dragging, x );

			return;

		}

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

		const text_field& current = active_field ( );

		if ( !measurer )
			return current.length;

		float target = x - ( field_areas [ focus ].x + 10.0f );
		int32_t best = 0;
		float distance = 1e9f;
		wchar_t prefix [ text_field::room + 1 ];

		for ( int32_t i = 0; i <= current.length; i++ ) {

			for ( int32_t j = 0; j < i; j++ )
				prefix [ j ] = focus == 2 ? L'\x2022' : current.text [ j ];

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
			focus = base::clamp ( press_index, 0, 3 );
			caret_since = now;
			active_field ( ).move ( caret_from ( x ), GetKeyState ( VK_SHIFT ) < 0 );

		} else if ( sheet != SHEET_NONE )
			field_focused = false;

		if ( press_action == ACTION_SLIDER && editing ( ) ) {

			dragging = press_index;
			set_slider ( dragging, x );

		}

	}

	void manager::set_slider ( int32_t which, float x ) {

		rect area = slider_areas [ which & 3 ];

		if ( area.w <= 0.0f )
			return;

		float position = base::clamp ( ( x - area.x ) / area.w, 0.0f, 1.0f );

		if ( which == 0 ) {

			uint32_t top = base::max ( host_memory, 1024u );
			uint32_t value = 256 + static_cast < uint32_t > ( position * static_cast < float > ( top - 256 ) );
			uint32_t step = value < 2048 ? 128u : ( value < 16384 ? 512u : 1024u );

			form.memory_mb = base::clamp ( snap ( value, step ), 256u, top );

		} else if ( which == 1 ) {

			uint32_t top = base::max ( host_logical, 1u );

			form.processors = base::clamp ( 1 + static_cast < uint32_t > ( position * static_cast < float > ( top - 1 ) + 0.5f ), 1u, top );

		} else if ( which == 2 )
			form.disk_gb = disk_from ( position );
		else
			form.cap = base::clamp ( snap ( 10 + static_cast < uint32_t > ( position * 90.0f ), 5 ), 10u, 100u );

	}

	void manager::apply_preset ( int32_t index ) {

		form.preset = index;

		if ( !known_presets || index < 0 || index >= known_count )
			return;

		const preset_card& item = known_presets [ index ];

		form.modern = item.modern;

		if ( item.modern ) {

			form.memory_mb = base::min ( item.memory_mb, base::max ( host_memory / 2, 512u ) );
			form.processors = base::min ( item.processors, base::max ( host_logical / 2, 1u ) );
			form.disk_gb = item.disk_gb ? item.disk_gb : 16;
			form.uefi = item.uefi;

		}

		form.windows = item.windows;

		if ( !form.windows && focus )
			focus = 0;

	}

	manager_action manager::mouse_up ( float x, float y, int32_t& index ) {

		const hit* found = find ( x, y );
		manager_action action = found ? found->action : ACTION_NONE;

		index = found ? found->index : -1;

		if ( dragging >= 0 ) {

			dragging = -1;
			press_action = ACTION_NONE;

			return ACTION_NONE;

		}

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
				apply_preset ( index );
				return ACTION_NONE;

			case ACTION_MEMORY:
				form.memory = memory_choices [ base::clamp ( index, 0, 2 ) ];
				return ACTION_NONE;

			case ACTION_EJECT:

				if ( index < 2 )
					form.floppies [ index ] [ 0 ] = 0;
				else if ( index == 2 )
					form.iso [ 0 ] = 0;
				else
					form.disk [ 0 ] = 0;

				return ACTION_NONE;

			case ACTION_DISK_MODE:

				if ( index == 1 )
					return ACTION_PICK_DISK;

				form.disk_mode = index;

				return ACTION_NONE;

			case ACTION_BOOT:
				form.disk_first = index == 1;
				return ACTION_NONE;

			case ACTION_FIRMWARE:
				form.uefi = index == 1;
				return ACTION_NONE;

			case ACTION_NETWORK:
				form.network = index == 0;
				return ACTION_NONE;

			case ACTION_CLIPBOARD:
				form.clipboard = static_cast < uint8_t > ( index & 3 );
				return ACTION_NONE;

			case ACTION_FILE_SHARING:
				form.file_sharing = static_cast < uint8_t > ( index & 3 );
				return ACTION_NONE;

			case ACTION_UNATTENDED:
				form.unattended = index == 0;

				if ( !form.unattended && focus )
					focus = 0;

				return ACTION_NONE;

			case ACTION_CATEGORY:
				category = index;
				field_focused = index == 0 && editing ( );
				focus = 0;
				return ACTION_NONE;

			case ACTION_SCREEN:

				if ( index >= 0 && index < vm::screen_choice_count ) {

					form.screen_width = vm::screen_choices ( ) [ index ].width;
					form.screen_height = vm::screen_choices ( ) [ index ].height;

				}

				return ACTION_NONE;

			case ACTION_FRAME_RATE:
				form.frame_rate = vm::frame_choices ( ) [ base::clamp ( index, 0, vm::frame_choice_count - 1 ) ];
				return ACTION_NONE;

			case ACTION_SCALING:
				form.scaling = static_cast < uint8_t > ( base::clamp ( index, 0, 2 ) );
				return ACTION_NONE;

			case ACTION_FILTER:
				form.smooth = index == 1;
				return ACTION_NONE;

			case ACTION_COUNTER:
				form.frame_counter = index == 1;
				return ACTION_NONE;

			case ACTION_LAN:
				form.local_network = index == 0;
				return ACTION_NONE;

			case ACTION_ROUTE:
				form.use_proxy = index == 1;

				if ( form.use_proxy && sheet == SHEET_SETTINGS && editing ( ) ) {

					focus = 3;
					field_focused = true;

				}

				return ACTION_NONE;

			case ACTION_IDENTITY:
				form.fresh_identity = index == 1;
				return ACTION_NONE;

			case ACTION_DISPOSABLE:
				form.disposable = index == 1;
				return ACTION_NONE;

			case ACTION_PREVIEW:
				form.keep_preview = index == 0;
				return ACTION_NONE;

			case ACTION_INTERNET:
				form.ask_internet = index == 1;
				return ACTION_NONE;

			case ACTION_SLIDER:
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

				if ( sheet == SHEET_NEW && name_valid ( ) && ( !asks_user ( ) || user_valid ( ) ) )
					return ACTION_CREATE;

				if ( sheet == SHEET_SETTINGS && editing ( ) && name_valid ( ) && proxy_valid ( ) )
					return ACTION_SAVE;

				if ( sheet == SHEET_LAUNCH )
					return ACTION_LAUNCH;

				return ACTION_NONE;

			}

			if ( code == VK_TAB && editing ( ) ) {

				if ( asks_user ( ) ) {

					focus = field_focused ? ( focus + ( shift ? 2 : 1 ) ) % 3 : 0;
					field_focused = true;

				} else {

					focus = 0;
					field_focused = !field_focused;

				}

				caret_since = last_paint;

				return ACTION_NONE;

			}

			if ( !field_focused || !editing ( ) )
				return ACTION_NONE;

			text_field& name = active_field ( );

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

		active_field ( ).insert ( text );
		caret_since = last_paint;

	}

	void manager::paste ( const wchar_t* value ) {

		if ( field_focused && editing ( ) && value )
			active_field ( ).insert ( value );

	}

	bool manager::user_valid ( ) const {

		return vm::user_name_valid ( form.user.text );

	}

	bool manager::proxy_valid ( ) const {

		if ( !form.use_proxy && !form.proxy.length )
			return true;

		char narrow [ text_field::room + 1 ] = { };

		for ( int32_t i = 0; i < form.proxy.length; i++ ) {

			if ( form.proxy.text [ i ] > 127 )
				return false;

			narrow [ i ] = static_cast < char > ( form.proxy.text [ i ] );

		}

		return vm::valid_proxy ( narrow, form.proxy.length );

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
		category = 0;
		focus = 0;
		dragging = -1;
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

			} else if ( enabled && glow ( target, i + segmented_offset ) > 0.01f )
				gpu.fill ( segment, corner - 1.0f, fade ( colors.hover, glow ( target, i + segmented_offset ) ) );

			gpu.text ( labels [ i ], i == selected ? render::FONT_SMALL_STRONG : render::FONT_SMALL, segment, enabled ? ( i == selected ? colors.text : colors.muted ) : colors.faint, render::ALIGN_CENTER );

			if ( enabled )
				add_hit ( segment, target, i + segmented_offset );

		}

	}

	void manager::field ( render::gpu& gpu, const theme& colors, rect area, double now, int32_t which, const wchar_t* placeholder, bool masked ) {

		const text_field& name = which == 1 ? form.user : ( which == 2 ? form.password : ( which == 3 ? form.proxy : form.name ) );
		bool active = field_focused && editing ( ) && focus == which;
		float left = area.x + 10.0f;
		int32_t from = 0;
		int32_t to = 0;
		wchar_t prefix [ text_field::room + 1 ];
		wchar_t shown [ text_field::room + 1 ];

		for ( int32_t j = 0; j <= name.length; j++ )
			shown [ j ] = masked && j < name.length ? L'\x2022' : name.text [ j ];

		auto offset = [ & ] ( int32_t count ) {

			for ( int32_t j = 0; j < count; j++ )
				prefix [ j ] = shown [ j ];

			prefix [ count ] = 0;

			return count ? gpu.measure ( prefix, render::FONT_BODY ) : 0.0f;

		};

		field_areas [ which ] = area;
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
			gpu.text ( shown, render::FONT_BODY, rect { left, area.y, area.w - 20.0f, area.h }, editing ( ) ? colors.text : colors.muted, render::ALIGN_LEFT );
		else if ( !active )
			gpu.text ( placeholder, render::FONT_BODY, rect { left, area.y, area.w - 20.0f, area.h }, colors.faint, render::ALIGN_LEFT );

		if ( active && ( static_cast < int64_t > ( ( now - caret_since ) / 0.53 ) & 1 ) == 0 ) {

			float x = math::round ( left + offset ( name.caret ) ) + 0.5f;

			gpu.line ( x, area.y + 7.0f, x, area.bottom ( ) - 7.0f, 1.0f, colors.text );

		}

		if ( editing ( ) )
			add_hit ( area, ACTION_FIELD, which );

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
		host_memory = base::max ( state.host_memory_mb, 1024u );
		host_logical = base::max ( state.host_logical, 1u );
		host_physical = base::max ( state.host_physical, 1u );
		known_presets = state.presets;
		known_count = state.preset_count;

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

		if ( usable && state.machines [ state.selected ].modern )
			x = tool ( gpu, colors, x, L"Start options", render::ICON_LOCK, ACTION_LAUNCH_OPTIONS, !running );

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
				else if ( card.modern ) {

					text.add ( card.kind ).add ( L"  \x00B7  " );
					memory_text ( text, card.memory_mb );

				} else
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
		gpu.text ( L"A machine is a modern PC or an IBM PC/XT with its own disks and memory.", render::FONT_BODY, rect { x, y + 76.0f, 400.0f, 20.0f }, colors.muted, render::ALIGN_LEFT );
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

		if ( card.modern ) {

			base::writer processors;
			base::writer cap;
			base::writer disk;

			memory_text ( memory, card.memory_mb );
			processors.number ( card.processors ).add ( card.processors == 1 ? L" processor" : L" processors" );
			cap.number ( card.cap ).add ( L"% of each core" );

			if ( card.disk [ 0 ] ) {

				disk.add ( file_name ( card.disk ) );

				if ( card.disk_bytes )
					disk.add ( L"  \x00B7  " ).number ( static_cast < int64_t > ( card.disk_bytes >> 30 ) ).add ( L" GB" );

			} else
				disk.add ( L"None" );

			section ( gpu, colors, column, top, side, L"SYSTEM" );
			property ( gpu, colors, column, top, side, L"Machine", card.kind, false );
			property ( gpu, colors, column, top, side, L"Processors", processors.text ( ), false );
			property ( gpu, colors, column, top, side, L"CPU cap", cap.text ( ), false );
			property ( gpu, colors, column, top, side, L"Memory", memory.text ( ), false );
			top += 14.0f;
			section ( gpu, colors, column, top, side, L"STORAGE" );
			property ( gpu, colors, column, top, side, L"CD/DVD", card.iso [ 0 ] ? file_name ( card.iso ) : L"Empty", !card.iso [ 0 ] );
			property ( gpu, colors, column, top, side, L"Hard disk", disk.text ( ), !card.disk [ 0 ] );
			top += 14.0f;

		} else {

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

		}
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

	float manager::file_row ( render::gpu& gpu, const theme& colors, float x, float y, float inner, const wchar_t* label, const wchar_t* path, manager_action browse, manager_action eject, int32_t index, bool enabled, const wchar_t* empty ) {

		bool present = path [ 0 ] != 0;
		float control_x = x + label_column;
		float control_width = inner - label_column;
		float buttons = present ? 84.0f + 6.0f + 64.0f : 84.0f;
		rect slot = { control_x, y, control_width - buttons - 6.0f, control_height };

		gpu.text ( label, render::FONT_BODY, rect { x, y, label_column, control_height }, enabled ? colors.muted : colors.faint, render::ALIGN_LEFT );
		gpu.fill ( slot, corner, colors.inset );
		gpu.stroke ( slot, corner, 1.0f, colors.divider );
		gpu.text ( present ? file_name ( path ) : empty, render::FONT_BODY, rect { slot.x + 10.0f, slot.y, slot.w - 20.0f, slot.h }, present && enabled ? colors.text : colors.faint, render::ALIGN_LEFT );
		button ( gpu, colors, rect { slot.right ( ) + 6.0f, y, 84.0f, control_height }, L"Browse\x2026", browse, 1, enabled, index );

		if ( present )
			button ( gpu, colors, rect { slot.right ( ) + 6.0f + 84.0f + 6.0f, y, 64.0f, control_height }, eject == ACTION_EJECT && index >= 2 ? L"Remove" : L"Eject", eject, 2, enabled, index );

		return y + control_height + 10.0f;

	}

	float manager::paint_xt_system ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled ) {

		static const wchar_t* const memory_labels [ 3 ] = { L"256 KB", L"512 KB", L"640 KB" };
		static const wchar_t* const bios_labels [ 2 ] = { L"Coldboot BIOS", L"Custom ROM\x2026" };
		float control_x = x + label_column;
		float control_width = inner - label_column;
		int32_t memory_index = form.memory <= 256 ? 0 : ( form.memory <= 512 ? 1 : 2 );
		color label_tint = enabled ? colors.muted : colors.faint;

		gpu.text ( L"Memory", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, control_width, control_height }, memory_labels, 3, memory_index, ACTION_MEMORY, enabled );
		y += control_height + 10.0f;
		gpu.text ( L"BIOS", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, control_width, control_height }, bios_labels, 2, form.custom_bios ? 1 : 0, ACTION_BIOS_BUILTIN, enabled );
		y += control_height + 6.0f;
		gpu.text ( form.custom_bios && form.bios [ 0 ] ? file_name ( form.bios ) : L"Built in, assembled from source at power on", render::FONT_SMALL, rect { control_x, y, control_width, 18.0f }, colors.faint, render::ALIGN_LEFT );

		return y + 30.0f;

	}

	float manager::paint_xt_storage ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled ) {

		y = file_row ( gpu, colors, x, y, inner, L"Floppy A", form.floppies [ 0 ], ACTION_PICK_FLOPPY, ACTION_EJECT, 0, enabled, L"Empty" );

		return file_row ( gpu, colors, x, y, inner, L"Floppy B", form.floppies [ 1 ], ACTION_PICK_FLOPPY, ACTION_EJECT, 1, enabled, L"Empty" );

	}

	float manager::paint_machine_fields ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled ) {

		y = paint_xt_system ( gpu, colors, x, y, inner, enabled );

		return paint_xt_storage ( gpu, colors, x, y, inner, enabled );

	}

	void manager::slider ( render::gpu& gpu, const theme& colors, rect area, int32_t which, bool enabled ) {

		float middle = area.y + area.h * 0.5f;
		float position = 0.0f;
		uint32_t top = base::max ( host_memory, 1024u );

		slider_areas [ which ] = area;

		if ( which == 0 )
			position = static_cast < float > ( form.memory_mb - base::min ( form.memory_mb, 256u ) ) / static_cast < float > ( top - 256 );
		else if ( which == 1 )
			position = host_logical > 1 ? static_cast < float > ( form.processors - 1 ) / static_cast < float > ( host_logical - 1 ) : 1.0f;
		else if ( which == 2 )
			position = disk_position ( form.disk_gb );
		else
			position = static_cast < float > ( form.cap - 10 ) / 90.0f;

		position = base::clamp ( position, 0.0f, 1.0f );
		gpu.fill ( rect { area.x, middle - 2.0f, area.w, 4.0f }, 2.0f, colors.divider );

		if ( which <= 1 ) {

			float safe = 0.0f;
			float warm = 0.0f;

			if ( which == 0 ) {

				safe = static_cast < float > ( top / 2 - 256 ) / static_cast < float > ( top - 256 );
				warm = static_cast < float > ( top * 4 / 5 - 256 ) / static_cast < float > ( top - 256 );

			} else {

				safe = host_logical > 1 ? static_cast < float > ( host_physical - 1 ) / static_cast < float > ( host_logical - 1 ) : 1.0f;
				warm = 1.0f;

			}

			gpu.fill ( rect { area.x, middle - 2.0f, area.w * safe, 4.0f }, 2.0f, fade ( colors.led_green, enabled ? 0.55f : 0.25f ) );
			gpu.fill ( rect { area.x + area.w * safe, middle - 2.0f, area.w * ( warm - safe ), 4.0f }, 0.0f, fade ( colors.accent, enabled ? 0.55f : 0.25f ) );

			if ( warm < 1.0f )
				gpu.fill ( rect { area.x + area.w * warm, middle - 2.0f, area.w * ( 1.0f - warm ), 4.0f }, 2.0f, fade ( colors.danger, enabled ? 0.55f : 0.25f ) );

		} else
			gpu.fill ( rect { area.x, middle - 2.0f, area.w * position, 4.0f }, 2.0f, enabled ? colors.accent : colors.faint );

		float x = area.x + area.w * position;
		float amount = enabled ? base::max ( glow ( ACTION_SLIDER, which ), dragging == which ? 1.0f : 0.0f ) : 0.0f;

		gpu.circle ( x, middle, 8.0f + amount, colors.raised );
		gpu.ring ( x, middle, 8.0f + amount, 1.0f, enabled ? colors.divider : colors.faint );
		gpu.circle ( x, middle, 3.5f, enabled ? colors.accent : colors.faint );

		if ( enabled )
			add_hit ( rect { area.x - 10.0f, area.y, area.w + 20.0f, area.h }, ACTION_SLIDER, which );

	}

	float manager::paint_pc_system ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled, bool full ) {

		static const wchar_t* const boot_labels [ 2 ] = { L"CD/DVD first", L"Hard disk first" };
		static const wchar_t* const firmware_labels [ 2 ] = { L"BIOS", L"UEFI" };
		static const wchar_t* const network_labels [ 2 ] = { L"Internet (NAT)", L"No internet" };
		float control_x = x + label_column;
		float track = inner - label_column - 96.0f;
		color label_tint = enabled ? colors.muted : colors.faint;
		base::writer memory;
		base::writer processors;
		base::writer host;

		memory_text ( memory, form.memory_mb );
		processors.number ( form.processors ).add ( form.processors == 1 ? L" CPU" : L" CPUs" );
		gpu.text ( L"Memory", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		slider ( gpu, colors, rect { control_x + 8.0f, y, track - 16.0f, control_height }, 0, enabled );
		gpu.text ( memory.text ( ), render::FONT_STRONG, rect { control_x + track, y, 96.0f, control_height }, enabled ? colors.text : colors.faint, render::ALIGN_RIGHT );
		y += control_height + 10.0f;
		gpu.text ( L"Processors", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		slider ( gpu, colors, rect { control_x + 8.0f, y, track - 16.0f, control_height }, 1, enabled );
		gpu.text ( processors.text ( ), render::FONT_STRONG, rect { control_x + track, y, 96.0f, control_height }, enabled ? colors.text : colors.faint, render::ALIGN_RIGHT );
		y += control_height + 10.0f;

		if ( full ) {

			base::writer cap;

			cap.number ( form.cap ).add ( L"%" );
			gpu.text ( L"CPU cap", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
			slider ( gpu, colors, rect { control_x + 8.0f, y, track - 16.0f, control_height }, 3, enabled );
			gpu.text ( cap.text ( ), render::FONT_STRONG, rect { control_x + track, y, 96.0f, control_height }, enabled ? colors.text : colors.faint, render::ALIGN_RIGHT );
			y += control_height + 10.0f;
			gpu.text ( L"Boot order", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
			segmented ( gpu, colors, rect { control_x, y, inner - label_column, control_height }, boot_labels, 2, form.disk_first ? 1 : 0, ACTION_BOOT, enabled );
			y += control_height + 10.0f;
			gpu.text ( L"Firmware", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
			segmented ( gpu, colors, rect { control_x, y, inner - label_column, control_height }, firmware_labels, 2, form.uefi ? 1 : 0, ACTION_FIRMWARE, enabled );
			y += control_height + 10.0f;
			gpu.text ( L"Network", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
			segmented ( gpu, colors, rect { control_x, y, inner - label_column, control_height }, network_labels, 2, form.network ? 0 : 1, ACTION_NETWORK, enabled );
			y += control_height + 10.0f;

		}

		host.add ( L"This PC has " );
		memory_text ( host, host_memory );
		host.add ( L", " ).number ( host_physical ).add ( L" cores and " ).number ( host_logical ).add ( L" threads. Green is comfortable." );
		gpu.text ( host.text ( ), render::FONT_SMALL, rect { control_x, y - 4.0f, inner - label_column, 18.0f }, colors.faint, render::ALIGN_LEFT );

		return y + 22.0f;

	}

	float manager::paint_pc_sharing ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled ) {

		static const wchar_t* const share_labels [ 4 ] = { L"Off", L"To machine", L"From machine", L"Both ways" };
		float control_x = x + label_column;
		color label_tint = enabled ? colors.muted : colors.faint;

		gpu.text ( L"Clipboard", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, inner - label_column, control_height }, share_labels, 4, form.clipboard & 3, ACTION_CLIPBOARD, enabled );
		y += control_height + 10.0f;
		gpu.text ( L"Files", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, inner - label_column, control_height }, share_labels, 4, form.file_sharing & 3, ACTION_FILE_SHARING, enabled );
		y += control_height + 14.0f;

		static const wchar_t* const notes [ 4 ] = {

			L"Text and files copied on one side paste on the other.",
			L"\"To machine\" only lets things in, which keeps your PC safe.",
			L"Sharing needs Coldboot Tools. The window has Install tools.",
			L"Clipboard files stop at 1 GB. Send files has no limit.",

		};

		for ( const wchar_t* note : notes ) {

			gpu.text ( note, render::FONT_SMALL, rect { control_x, y, inner - label_column, 18.0f }, colors.faint, render::ALIGN_LEFT );
			y += 20.0f;

		}

		return y;

	}

	float manager::paint_pc_display ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled ) {

		static const wchar_t* const sizes [ vm::screen_choice_count ] = { L"Automatic", L"1024 \x00D7 768", L"1280 \x00D7 720", L"1280 \x00D7 800", L"1366 \x00D7 768", L"1600 \x00D7 900", L"1920 \x00D7 1080", L"2560 \x00D7 1440" };
		static const wchar_t* const rates [ vm::frame_choice_count ] = { L"Display", L"30", L"60", L"90", L"120", L"144" };
		static const wchar_t* const scalings [ 3 ] = { L"Fit window", L"Whole steps", L"Actual size" };
		static const wchar_t* const filters [ 2 ] = { L"Sharp", L"Smooth" };
		static const wchar_t* const counters [ 2 ] = { L"Hidden", L"Shown" };
		float control_x = x + label_column;
		float span = inner - label_column;
		color label_tint = enabled ? colors.muted : colors.faint;
		int32_t size_index = -1;
		int32_t rate_index = -1;

		for ( int32_t i = 0; i < vm::screen_choice_count; i++ ) {

			if ( vm::screen_choices ( ) [ i ].width == form.screen_width && vm::screen_choices ( ) [ i ].height == form.screen_height )
				size_index = i;

		}

		for ( int32_t i = 0; i < vm::frame_choice_count; i++ ) {

			if ( vm::frame_choices ( ) [ i ] == form.frame_rate )
				rate_index = i;

		}

		gpu.text ( L"Resolution", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, sizes, 4, size_index < 4 ? size_index : -1, ACTION_SCREEN, enabled );
		y += control_height + 4.0f;

		const wchar_t* const* upper = sizes + 4;

		segmented_offset = 4;
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, upper, 4, size_index >= 4 ? size_index - 4 : -1, ACTION_SCREEN, enabled );
		segmented_offset = 0;
		y += control_height + 10.0f;
		gpu.text ( L"Frame rate", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, rates, vm::frame_choice_count, rate_index, ACTION_FRAME_RATE, enabled );
		y += control_height + 10.0f;
		gpu.text ( L"Scaling", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, scalings, 3, form.scaling, ACTION_SCALING, enabled );
		y += control_height + 10.0f;
		gpu.text ( L"Filter", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, filters, 2, form.smooth ? 1 : 0, ACTION_FILTER, enabled );
		y += control_height + 10.0f;
		gpu.text ( L"Frame counter", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, counters, 2, form.frame_counter ? 1 : 0, ACTION_COUNTER, enabled );
		y += control_height + 12.0f;

		static const wchar_t* const notes [ 2 ] = {

			L"Chosen resolutions apply at start. BIOS machines need the tools.",
			L"\"Display\" follows your monitor's refresh rate with no tearing.",

		};

		for ( const wchar_t* note : notes ) {

			gpu.text ( note, render::FONT_SMALL, rect { control_x, y, span, 18.0f }, colors.faint, render::ALIGN_LEFT );
			y += 20.0f;

		}

		return y;

	}

	float manager::paint_pc_privacy ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled, double now, bool launching ) {

		static const wchar_t* const lan_labels [ 2 ] = { L"Allowed", L"Blocked" };
		static const wchar_t* const route_labels [ 2 ] = { L"Direct", L"SOCKS5 proxy" };
		static const wchar_t* const identity_labels [ 2 ] = { L"Keep the same", L"New every start" };
		static const wchar_t* const disposable_labels [ 2 ] = { L"Keep them", L"Forget at shutdown" };
		static const wchar_t* const preview_labels [ 2 ] = { L"Saved", L"Never saved" };
		static const wchar_t* const internet_labels [ 2 ] = { L"Open", L"Ask first" };
		float control_x = x + label_column;
		float span = inner - label_column;
		color label_tint = enabled ? colors.muted : colors.faint;
		bool can_route = !launching || form.proxy.length > 0;

		gpu.text ( L"Internet", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, internet_labels, 2, form.ask_internet ? 1 : 0, ACTION_INTERNET, enabled );
		y += control_height + 10.0f;
		gpu.text ( L"Your network", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, lan_labels, 2, form.local_network ? 0 : 1, ACTION_LAN, enabled );
		y += control_height + 10.0f;
		gpu.text ( L"Route", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, route_labels, 2, form.use_proxy && can_route ? 1 : 0, ACTION_ROUTE, enabled && can_route );
		y += control_height + 10.0f;

		if ( !launching && form.use_proxy ) {

			gpu.text ( L"Proxy", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
			field ( gpu, colors, rect { control_x, y, span, control_height }, now, 3, L"127.0.0.1:9050", false );
			y += control_height + 10.0f;

		}

		gpu.text ( L"Hardware IDs", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, identity_labels, 2, form.fresh_identity ? 1 : 0, ACTION_IDENTITY, enabled );
		y += control_height + 10.0f;
		gpu.text ( L"Disk changes", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, span, control_height }, disposable_labels, 2, form.disposable ? 1 : 0, ACTION_DISPOSABLE, enabled );
		y += control_height + 10.0f;

		if ( !launching ) {

			gpu.text ( L"Screen preview", render::FONT_BODY, rect { x, y, label_column, control_height }, label_tint, render::ALIGN_LEFT );
			segmented ( gpu, colors, rect { control_x, y, span, control_height }, preview_labels, 2, form.keep_preview ? 0 : 1, ACTION_PREVIEW, enabled );
			y += control_height + 10.0f;

		}

		const wchar_t* notes [ 2 ] = {

			form.use_proxy && can_route ? ( proxy_valid ( ) ? L"All traffic and names go through the proxy. Nothing else leaves." : L"Type the proxy as host:port, like 127.0.0.1:9050 for Tor." ) : ( form.ask_internet ? L"Each app asks in the machine's window before it can connect." : L"Blocking keeps the machine away from your PC and other devices." ),
			form.disposable ? L"Writes go to an encrypted scratch file. The key stays in memory." : ( launching && !can_route ? L"Set a proxy under Settings, Privacy to route through it." : L"New IDs mean a fresh MAC address and system UUID on every start." ),

		};

		bool wrong = form.use_proxy && can_route && !proxy_valid ( );

		y += 2.0f;

		for ( int32_t i = 0; i < 2; i++ ) {

			gpu.text ( notes [ i ], render::FONT_SMALL, rect { control_x, y, span, 18.0f }, wrong && i == 0 ? colors.danger : colors.faint, render::ALIGN_LEFT );
			y += 20.0f;

		}

		return y;

	}

	float manager::paint_pc_storage ( render::gpu& gpu, const theme& colors, float x, float y, float inner, bool enabled, bool creating ) {

		static const wchar_t* const disk_labels [ 3 ] = { L"New disk", L"Existing\x2026", L"None" };
		float control_x = x + label_column;
		float track = inner - label_column - 96.0f;

		y = file_row ( gpu, colors, x, y, inner, L"CD/DVD", form.iso, ACTION_PICK_ISO, ACTION_EJECT, 2, enabled, L"No ISO image" );

		if ( !creating )
			return file_row ( gpu, colors, x, y, inner, L"Hard disk", form.disk, ACTION_PICK_DISK, ACTION_EJECT, 3, enabled, L"None" );

		gpu.text ( L"Hard disk", render::FONT_BODY, rect { x, y, label_column, control_height }, enabled ? colors.muted : colors.faint, render::ALIGN_LEFT );
		segmented ( gpu, colors, rect { control_x, y, inner - label_column, control_height }, disk_labels, 3, form.disk_mode, ACTION_DISK_MODE, enabled );
		y += control_height + 10.0f;

		if ( form.disk_mode == 0 ) {

			base::writer size;

			size.number ( form.disk_gb ).add ( L" GB" );
			gpu.text ( L"Size", render::FONT_BODY, rect { x, y, label_column, control_height }, enabled ? colors.muted : colors.faint, render::ALIGN_LEFT );
			slider ( gpu, colors, rect { control_x + 8.0f, y, track - 16.0f, control_height }, 2, enabled );
			gpu.text ( size.text ( ), render::FONT_STRONG, rect { control_x + track, y, 96.0f, control_height }, colors.text, render::ALIGN_RIGHT );
			y += control_height + 6.0f;
			gpu.text ( L"Created instantly. It only takes space on your drive as it fills up.", render::FONT_SMALL, rect { control_x, y - 4.0f, inner - label_column, 18.0f }, colors.faint, render::ALIGN_LEFT );
			y += 18.0f;

		} else if ( form.disk_mode == 1 ) {

			gpu.text ( form.disk [ 0 ] ? form.disk : L"No disk chosen", render::FONT_SMALL, rect { control_x, y - 4.0f, inner - label_column, 18.0f }, colors.muted, render::ALIGN_LEFT );
			y += 18.0f;

		}

		return y;

	}

	void manager::paint_sheet ( render::gpu& gpu, const theme& colors, const manager_view& state, double now ) {

		bool remove = shown == SHEET_REMOVE;
		bool settings = shown == SHEET_SETTINGS;
		bool launch = shown == SHEET_LAUNCH;
		bool enabled = editing ( );
		float sheet_width = remove ? 460.0f : ( settings ? 700.0f : ( launch ? 600.0f : 660.0f ) );
		float content = 60.0f;
		int32_t rows = ( state.preset_count + 2 ) / 3;

		if ( launch )
			content = 30.0f + 5.0f * 40.0f + 46.0f;
		else if ( shown == SHEET_NEW ) {

			content = 54.0f + static_cast < float > ( rows ) * 36.0f + 30.0f;

			if ( form.modern ) {

				content += 40.0f + 40.0f + 40.0f + 22.0f + 40.0f;
				content += form.disk_mode == 0 ? 40.0f + 18.0f : ( form.disk_mode == 1 ? 18.0f : 0.0f );

				if ( form.windows )
					content += 40.0f + ( form.unattended ? 40.0f + 40.0f + 20.0f : 0.0f );

			} else
				content += 40.0f + 66.0f + 80.0f;

		} else if ( settings )
			content = 340.0f + ( subject_running ? 44.0f : 0.0f );

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

			if ( subject_running )
				gpu.text ( L"The machine is running. Close its window first.", render::FONT_BODY, rect { x, y, inner, 20.0f }, colors.muted, render::ALIGN_LEFT );
			else {

				gpu.text ( L"Removing it from the list keeps its folder and files.", render::FONT_BODY, rect { x, y, inner, 20.0f }, colors.muted, render::ALIGN_LEFT );
				gpu.text ( L"Deleting erases it and its hard disk for good. ISOs and floppies stay.", render::FONT_BODY, rect { x, y + 22.0f, inner, 20.0f }, colors.muted, render::ALIGN_LEFT );

			}

			button ( gpu, colors, rect { card.right ( ) - 20.0f - 80.0f, buttons, 80.0f, control_height }, L"Delete", ACTION_DELETE, 3, !subject_running );
			button ( gpu, colors, rect { card.right ( ) - 20.0f - 80.0f - 8.0f - 136.0f, buttons, 136.0f, control_height }, L"Remove from list", ACTION_FORGET, 1, !subject_running );
			button ( gpu, colors, rect { x, buttons, 80.0f, control_height }, L"Cancel", ACTION_CANCEL, 1, true );
			gpu.opacity = 1.0f;

			return;

		}

		if ( launch ) {

			base::writer title;

			title.add ( L"Start \x201C" ).add ( subject ).add ( L"\x201D" );
			gpu.text ( title.text ( ), render::FONT_TITLE, rect { x, card.y + 18.0f, inner, 24.0f }, colors.text, render::ALIGN_LEFT );
			gpu.text ( L"These apply to this start only. Settings keep the machine's usual choices.", render::FONT_SMALL, rect { x, y, inner, 18.0f }, colors.muted, render::ALIGN_LEFT );
			paint_pc_privacy ( gpu, colors, x, y + 30.0f, inner, true, now, true );
			button ( gpu, colors, rect { card.right ( ) - 20.0f - 80.0f, buttons, 80.0f, control_height }, L"Start", ACTION_LAUNCH, 0, true );
			button ( gpu, colors, rect { card.right ( ) - 20.0f - 80.0f - 8.0f - 80.0f, buttons, 80.0f, control_height }, L"Cancel", ACTION_CANCEL, 1, true );
			gpu.opacity = 1.0f;

			return;

		}

		gpu.text ( settings ? L"Machine settings" : L"New machine", render::FONT_TITLE, rect { x, card.y + 18.0f, inner, 24.0f }, colors.text, render::ALIGN_LEFT );

		if ( settings ) {

			static const wchar_t* const pages [ 6 ] = { L"General", L"System", L"Storage", L"Sharing", L"Display", L"Privacy" };
			float nav = 150.0f;
			int32_t page_count = form.modern ? 6 : 3;

			gpu.fill ( rect { card.x + nav, card.y + 56.0f, 1.0f, footer - card.y - 56.0f }, 0.0f, colors.divider );

			for ( int32_t i = 0; i < page_count; i++ ) {

				rect item = { card.x + 8.0f, card.y + 56.0f + static_cast < float > ( i ) * 34.0f, nav - 16.0f, 30.0f };

				if ( i == category ) {

					gpu.fill ( item, corner, colors.inset );
					gpu.fill ( rect { item.x, item.y + 8.0f, 3.0f, item.h - 16.0f }, 1.5f, colors.accent );

				} else if ( glow ( ACTION_CATEGORY, i ) > 0.01f )
					gpu.fill ( item, corner, fade ( colors.hover, glow ( ACTION_CATEGORY, i ) ) );

				gpu.text ( pages [ i ], i == category ? render::FONT_STRONG : render::FONT_BODY, rect { item.x + 14.0f, item.y, item.w - 20.0f, item.h }, i == category ? colors.text : colors.muted, render::ALIGN_LEFT );
				add_hit ( item, ACTION_CATEGORY, i );

			}

			x = card.x + nav + 24.0f;
			inner = card.right ( ) - 24.0f - x;

			if ( subject_running ) {

				rect notice = { x, y, inner, 32.0f };

				gpu.fill ( notice, corner, fade ( colors.accent, 0.1f ) );
				gpu.fill ( rect { notice.x, notice.y, 3.0f, notice.h }, 1.5f, colors.accent );
				gpu.text ( L"The machine is running. Close its window to change these settings.", render::FONT_SMALL, rect { x + 14.0f, y, inner - 24.0f, 32.0f }, colors.text, render::ALIGN_LEFT );
				y += 44.0f;

			}

			if ( category == 0 ) {

				gpu.text ( L"Name", render::FONT_BODY, rect { x, y, label_column, control_height }, enabled ? colors.muted : colors.faint, render::ALIGN_LEFT );
				field ( gpu, colors, rect { x + label_column, y, inner - label_column, control_height }, now, 0, L"Machine name", false );
				y += control_height;

				if ( form.name.length && !name_valid ( ) )
					gpu.text ( L"Names can't end with a space or dot, or use \\ / : * ? \" < > |", render::FONT_SMALL, rect { x + label_column, y + 4.0f, inner - label_column, 16.0f }, colors.danger, render::ALIGN_LEFT );

				y += 34.0f;

				if ( form.preset >= 0 && form.preset < state.preset_count ) {

					gpu.text ( L"Type", render::FONT_BODY, rect { x, y, label_column, 20.0f }, colors.muted, render::ALIGN_LEFT );
					gpu.text ( state.presets [ form.preset ].label, render::FONT_BODY, rect { x + label_column, y, inner - label_column, 20.0f }, colors.text, render::ALIGN_LEFT );
					gpu.text ( state.presets [ form.preset ].detail, render::FONT_SMALL, rect { x + label_column, y + 20.0f, inner - label_column, 18.0f }, colors.faint, render::ALIGN_LEFT );

				}

			} else if ( category == 1 ) {

				if ( form.modern )
					paint_pc_system ( gpu, colors, x, y, inner, enabled, true );
				else
					paint_xt_system ( gpu, colors, x, y, inner, enabled );

			} else if ( category == 3 && form.modern )
				paint_pc_sharing ( gpu, colors, x, y, inner, enabled );
			else if ( category == 4 && form.modern )
				paint_pc_display ( gpu, colors, x, y, inner, enabled );
			else if ( category == 5 && form.modern )
				paint_pc_privacy ( gpu, colors, x, y, inner, enabled, now, false );
			else if ( form.modern )
				paint_pc_storage ( gpu, colors, x, y, inner, enabled, false );
			else
				paint_xt_storage ( gpu, colors, x, y, inner, enabled );

			button ( gpu, colors, rect { card.right ( ) - 20.0f - 80.0f, buttons, 80.0f, control_height }, L"Save", ACTION_SAVE, 0, enabled && name_valid ( ) && proxy_valid ( ) );
			button ( gpu, colors, rect { card.right ( ) - 20.0f - 80.0f - 8.0f - 80.0f, buttons, 80.0f, control_height }, L"Cancel", ACTION_CANCEL, 1, true );
			gpu.opacity = 1.0f;

			return;

		}

		gpu.text ( L"Name", render::FONT_BODY, rect { x, y, label_column, control_height }, colors.muted, render::ALIGN_LEFT );
		field ( gpu, colors, rect { x + label_column, y, inner - label_column, control_height }, now, 0, L"Machine name", false );
		y += control_height;

		if ( form.name.length && !name_valid ( ) )
			gpu.text ( L"Names can't end with a space or dot, or use \\ / : * ? \" < > |", render::FONT_SMALL, rect { x + label_column, y + 4.0f, inner - label_column, 16.0f }, colors.danger, render::ALIGN_LEFT );

		y += 24.0f;
		gpu.text ( L"Type", render::FONT_BODY, rect { x, y, label_column, 34.0f }, colors.muted, render::ALIGN_LEFT );

		float column = ( inner - label_column - 16.0f ) / 3.0f;

		for ( int32_t i = 0; i < state.preset_count; i++ ) {

			const preset_card& item = state.presets [ i ];
			rect option = { x + label_column + static_cast < float > ( i % 3 ) * ( column + 8.0f ), y + static_cast < float > ( i / 3 ) * 36.0f, column, 32.0f };
			bool chosen = form.preset == i;

			gpu.fill ( option, corner, chosen ? fade ( colors.accent, 0.1f ) : colors.inset );
			gpu.stroke ( option, corner, 1.0f, chosen ? colors.accent : colors.divider );

			if ( !chosen && glow ( ACTION_PRESET, i ) > 0.01f )
				gpu.fill ( option, corner, fade ( colors.hover, glow ( ACTION_PRESET, i ) ) );

			gpu.ring ( option.x + 16.0f, option.y + 16.0f, 6.0f, 1.2f, chosen ? colors.accent : colors.muted );

			if ( chosen )
				gpu.circle ( option.x + 16.0f, option.y + 16.0f, 3.0f, colors.accent );

			gpu.text ( item.label, render::FONT_BODY, rect { option.x + 30.0f, option.y, option.w - 38.0f, option.h }, colors.text, render::ALIGN_LEFT );
			add_hit ( option, ACTION_PRESET, i );

		}

		y += static_cast < float > ( rows ) * 36.0f;

		if ( form.preset >= 0 && form.preset < state.preset_count )
			gpu.text ( state.presets [ form.preset ].detail, render::FONT_SMALL, rect { x + label_column, y, inner - label_column, 18.0f }, colors.faint, render::ALIGN_LEFT );

		y += 30.0f;

		if ( form.modern ) {

			y = paint_pc_storage ( gpu, colors, x, y, inner, true, true );
			y = paint_pc_system ( gpu, colors, x, y, inner, true, false );

			if ( form.windows ) {

				static const wchar_t* const setup_labels [ 2 ] = { L"Automatic", L"Ask me" };
				float control_x = x + label_column;

				gpu.text ( L"Windows setup", render::FONT_BODY, rect { x, y, label_column, control_height }, colors.muted, render::ALIGN_LEFT );
				segmented ( gpu, colors, rect { control_x, y, inner - label_column, control_height }, setup_labels, 2, form.unattended ? 0 : 1, ACTION_UNATTENDED, true );
				y += control_height + 10.0f;

				if ( form.unattended ) {

					gpu.text ( L"User name", render::FONT_BODY, rect { x, y, label_column, control_height }, colors.muted, render::ALIGN_LEFT );
					field ( gpu, colors, rect { control_x, y, inner - label_column, control_height }, now, 1, L"Your account in Windows", false );
					y += control_height + 10.0f;
					gpu.text ( L"Password", render::FONT_BODY, rect { x, y, label_column, control_height }, colors.muted, render::ALIGN_LEFT );
					field ( gpu, colors, rect { control_x, y, inner - label_column, control_height }, now, 2, L"None", true );
					y += control_height + 4.0f;

					const wchar_t* note = form.user.length && !user_valid ( ) ? L"User names are up to 20 characters, without \" / \\ [ ] : ; | = , + * ? < > @ %" : L"Setup runs by itself, makes this account, turns the privacy options off and installs Coldboot Tools.";

					gpu.text ( note, render::FONT_SMALL, rect { control_x, y, inner - label_column, 16.0f }, form.user.length && !user_valid ( ) ? colors.danger : colors.faint, render::ALIGN_LEFT );

				}

			}

		} else
			paint_machine_fields ( gpu, colors, x, y, inner, true );

		base::writer place;

		place.add ( L"Saved in " ).add ( state.location );
		gpu.text ( place.text ( ), render::FONT_SMALL, rect { x, buttons, inner - 88.0f - 8.0f - 80.0f - 16.0f, control_height }, colors.faint, render::ALIGN_LEFT );
		button ( gpu, colors, rect { card.right ( ) - 20.0f - 88.0f, buttons, 88.0f, control_height }, L"Create", ACTION_CREATE, 0, name_valid ( ) && ( !asks_user ( ) || user_valid ( ) ) && ( !form.modern || form.disk_mode != 1 || form.disk [ 0 ] ) );
		button ( gpu, colors, rect { card.right ( ) - 20.0f - 88.0f - 8.0f - 80.0f, buttons, 80.0f, control_height }, L"Cancel", ACTION_CANCEL, 1, true );
		gpu.opacity = 1.0f;

	}

}