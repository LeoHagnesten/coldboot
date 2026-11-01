#include "ui/shell.hh"

namespace ui {

	using render::rect;
	using render::color;
	using render::fade;

	void shell::paint_disks ( render::gpu& gpu, const theme& colors, const view& state, rect panel ) {

		float x = panel.x + 16.0f;
		float inner = panel.w - 32.0f;
		float y = panel.y + 14.0f;

		gpu.text ( L"Floppy drives", render::FONT_TITLE, rect { x, y, inner, 24.0f }, colors.text, render::ALIGN_LEFT );
		y += 34.0f;

		for ( int32_t d = 0; d < 2; d++ ) {

			const drive_view& drive = state.drives [ d ];
			rect card = { x, y, inner, 62.0f };
			rect badge = { card.x + 12.0f, card.y + 17.0f, 28.0f, 28.0f };

			gpu.fill ( card, 10.0f, colors.inset );
			gpu.fill ( badge, 7.0f, colors.raised );
			gpu.stroke ( badge, 7.0f, 1.0f, colors.divider );
			gpu.text ( d ? L"B" : L"A", render::FONT_STRONG, badge, colors.text, render::ALIGN_CENTER );

			if ( drive.light )
				gpu.circle ( badge.right ( ) - 1.0f, badge.y + 1.0f, 3.5f, colors.led_red );

			float buttons = 3.0f * 32.0f + 2.0f * 2.0f;
			float text_x = badge.right ( ) + 12.0f;
			float text_width = card.right ( ) - 12.0f - buttons - 10.0f - text_x;

			gpu.text ( drive.present ? drive.name : L"No disk", render::FONT_STRONG, rect { text_x, card.y + 11.0f, text_width, 20.0f }, drive.present ? colors.text : colors.muted, render::ALIGN_LEFT );

			base::writer detail;

			if ( drive.present ) {

				if ( drive.kilobytes >= 1000 )
					detail.fixed ( drive.kilobytes / 1000.0, drive.kilobytes % 100 ? 2 : 1 ).add ( L" MB" );
				else
					detail.number ( drive.kilobytes ).add ( L" KB" );

				detail.add ( drive.program ? L"  \x00B7  assembled from source" : ( drive.protect ? L"  \x00B7  write-protected" : L"  \x00B7  writable" ) );

			} else
				detail.add ( L"Insert or drop an image" );

			gpu.text ( detail.text ( ), render::FONT_SMALL, rect { text_x, card.y + 31.0f, text_width, 18.0f }, colors.muted, render::ALIGN_LEFT );

			float bx = card.right ( ) - 12.0f - 32.0f;
			float by = card.y + 15.0f;

			button ( gpu, colors, rect { bx, by, 32.0f, 32.0f }, d ? CONTROL_INSERT_B : CONTROL_INSERT_A, render::ICON_FOLDER, false, true, colors.text );
			bx -= 34.0f;
			button ( gpu, colors, rect { bx, by, 32.0f, 32.0f }, d ? CONTROL_PROTECT_B : CONTROL_PROTECT_A, drive.protect ? render::ICON_LOCK : render::ICON_UNLOCK, drive.protect, drive.present, colors.text );
			bx -= 34.0f;
			button ( gpu, colors, rect { bx, by, 32.0f, 32.0f }, d ? CONTROL_EJECT_B : CONTROL_EJECT_A, render::ICON_EJECT, false, drive.present, colors.text );
			y += card.h + 8.0f;

		}

		y += 6.0f;
		gpu.fill ( rect { x, y, inner, 1.0f }, 0.0f, colors.divider );
		y += 14.0f;
		gpu.text ( L"New blank disk", render::FONT_SMALL_STRONG, rect { x, y, inner, 18.0f }, colors.text, render::ALIGN_LEFT );
		y += 24.0f;

		static const control sizes [ 4 ] = { CONTROL_NEW_360, CONTROL_NEW_720, CONTROL_NEW_1200, CONTROL_NEW_1440 };
		static const wchar_t* const labels [ 4 ] = { L"360 KB", L"720 KB", L"1.2 MB", L"1.44 MB" };
		float pill = ( inner - 3.0f * 8.0f ) / 4.0f;

		for ( int32_t i = 0; i < 4; i++ ) {

			rect area = { x + ( pill + 8.0f ) * static_cast < float > ( i ), y, pill, 32.0f };

			gpu.fill ( area, 8.0f, colors.inset );

			if ( glow ( sizes [ i ] ) > 0.01f )
				gpu.fill ( area, 8.0f, fade ( colors.hover, glow ( sizes [ i ] ) ) );

			gpu.icon ( render::ICON_PLUS, area.x + 15.0f, area.y + 16.0f, 12.0f, colors.muted );
			gpu.text ( labels [ i ], render::FONT_SMALL, rect { area.x + 24.0f, area.y, area.w - 28.0f, area.h }, colors.text, render::ALIGN_LEFT );
			add_hit ( area, sizes [ i ] );

		}

		y += 44.0f;
		gpu.text ( L"Changes are written back when the drive motor stops.", render::FONT_SMALL, rect { x, y, inner, 18.0f }, colors.faint, render::ALIGN_LEFT );

	}

	void shell::paint_settings ( render::gpu& gpu, const theme& colors, const view& state, rect panel ) {

		float x = panel.x + 16.0f;
		float inner = panel.w - 32.0f;
		float y = panel.y + 14.0f;
		float control_width = 196.0f;
		float control_x = panel.right ( ) - 16.0f - control_width;

		gpu.text ( L"Settings", render::FONT_TITLE, rect { x, y, inner, 24.0f }, colors.text, render::ALIGN_LEFT );
		y += 36.0f;

		auto row = [ & ] ( const wchar_t* label, const control* ids, const wchar_t* const* labels, int32_t count, int32_t selected ) {

			gpu.text ( label, render::FONT_BODY, rect { x, y, control_x - x - 8.0f, 32.0f }, colors.text, render::ALIGN_LEFT );
			segmented ( gpu, colors, rect { control_x, y, control_width, 32.0f }, ids, labels, count, selected );
			y += 42.0f;

		};

		static const control speed_ids [ 2 ] = { CONTROL_SPEED_REAL, CONTROL_SPEED_TURBO };
		static const wchar_t* const speed_labels [ 2 ] = { L"4.77 MHz", L"Turbo" };
		static const control display_ids [ 2 ] = { CONTROL_DISPLAY_SHARP, CONTROL_DISPLAY_CRT };
		static const wchar_t* const display_labels [ 2 ] = { L"Sharp", L"CRT" };
		static const control theme_ids [ 3 ] = { CONTROL_THEME_SYSTEM, CONTROL_THEME_DARK, CONTROL_THEME_LIGHT };
		static const wchar_t* const theme_labels [ 3 ] = { L"System", L"Dark", L"Light" };
		static const control floppy_ids [ 2 ] = { CONTROL_FLOPPY_REAL, CONTROL_FLOPPY_FAST };
		static const wchar_t* const floppy_labels [ 2 ] = { L"Real speed", L"Fast" };
		static const control memory_ids [ 3 ] = { CONTROL_MEMORY_256, CONTROL_MEMORY_512, CONTROL_MEMORY_640 };
		static const wchar_t* const memory_labels [ 3 ] = { L"256 KB", L"512 KB", L"640 KB" };
		static const control bios_ids [ 2 ] = { CONTROL_BIOS_BUILTIN, CONTROL_BIOS_CUSTOM };
		static const wchar_t* const bios_labels [ 2 ] = { L"Coldboot", L"Custom" };

		row ( L"Speed", speed_ids, speed_labels, 2, state.turbo ? 1 : 0 );
		row ( L"Display", display_ids, display_labels, 2, state.crt ? 1 : 0 );
		row ( L"Theme", theme_ids, theme_labels, 3, base::clamp ( state.theme, 0, 2 ) );
		row ( L"Floppy drives", floppy_ids, floppy_labels, 2, state.fast_floppy ? 1 : 0 );
		row ( L"Memory", memory_ids, memory_labels, 3, state.memory <= 256 ? 0 : ( state.memory <= 512 ? 1 : 2 ) );
		row ( L"BIOS", bios_ids, bios_labels, 2, state.custom_bios ? 1 : 0 );

		const wchar_t* note = state.custom_bios ? state.bios_name : L"Built in, assembled from source at power on";

		gpu.text ( note, render::FONT_SMALL, rect { x, y - 6.0f, inner, 18.0f }, colors.muted, render::ALIGN_LEFT );
		y += 18.0f;
		gpu.text ( L"Memory and BIOS changes apply at the next power on.", render::FONT_SMALL, rect { x, y, inner, 18.0f }, colors.faint, render::ALIGN_LEFT );

	}

}