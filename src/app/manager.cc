#include "app/dialogs.hh"
#include "app/settings.hh"
#include "base/files.hh"
#include "render/gpu.hh"
#include "ui/manager.hh"
#include "vm/library.hh"
#include "vm/unattend.hh"
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

namespace app {

	struct preview_image {

		base::list < uint32_t > pixels;
		int32_t width = 0;
		int32_t height = 0;

	};

	struct card_text {

		wchar_t folder [ MAX_PATH ] = { };
		wchar_t problem [ 120 ] = { };
		wchar_t bios [ MAX_PATH ] = { };

	};

	static bool ends_with ( const wchar_t* path, const wchar_t* tail ) {

		int32_t length = base::text_length ( path );
		int32_t size = base::text_length ( tail );

		if ( length < size )
			return false;

		for ( int32_t i = 0; i < size; i++ ) {

			if ( ( path [ length - size + i ] | 0x20 ) != ( tail [ i ] | 0x20 ) )
				return false;

		}

		return true;

	}

	static int32_t preset_index ( const char* id ) {

		int32_t count = 0;
		const vm::preset* table = vm::presets ( count );

		for ( int32_t i = 0; i < count; i++ ) {

			if ( base::same_text ( table [ i ].id, id ) )
				return i;

		}

		return 0;

	}

	static constexpr UINT_PTR animation_timer = 1;
	static constexpr UINT_PTR status_timer = 2;
	static constexpr UINT_PTR smoke_timer = 3;

	static HWND window = nullptr;
	static render::gpu* display = nullptr;
	static ui::manager* view = nullptr;
	static vm::library* shelf = nullptr;
	static settings prefs;
	static bool light = false;
	static int32_t selected = -1;
	static base::list < ui::machine_card > cards;
	static base::list < card_text > texts;
	static preview_image preview;
	static ui::preset_card choices [ 12 ];
	static int32_t choice_count = 0;
	static wchar_t home [ MAX_PATH ] = { };
	static vm::host_info host;
	static LARGE_INTEGER frequency;
	static bool animating = false;
	static bool double_click = false;
	static bool smoke = false;
	static int32_t smoke_step = 0;
	static wchar_t smoke_folder [ MAX_PATH ] = { };
	static base::writer* smoke_report = nullptr;

	static double now_seconds ( ) {

		LARGE_INTEGER now;

		QueryPerformanceCounter ( &now );

		return static_cast < double > ( now.QuadPart ) / static_cast < double > ( frequency.QuadPart );

	}

	static float dip_scale ( ) {

		return static_cast < float > ( GetDpiForWindow ( window ) ) / 96.0f;

	}

	static void copy_text ( wchar_t* out, const wchar_t* text, int32_t room ) {

		int32_t length = 0;

		for ( ; text && text [ length ] && length < room - 1; length++ )
			out [ length ] = text [ length ];

		out [ length ] = 0;

	}

	static const wchar_t* file_name ( const wchar_t* path ) {

		const wchar_t* name = path;

		for ( const wchar_t* at = path; *at; at++ ) {

			if ( *at == L'\\' || *at == L'/' )
				name = at + 1;

		}

		return name;

	}

	static void apply_theme ( ) {

		light = use_light ( prefs );

		BOOL dark = light ? FALSE : TRUE;
		COLORREF caption = light ? RGB ( 0xF8, 0xF8, 0xF6 ) : RGB ( 0x15, 0x16, 0x1A );
		COLORREF text = light ? RGB ( 0x1A, 0x1B, 0x1E ) : RGB ( 0xEC, 0xED, 0xEF );

		DwmSetWindowAttribute ( window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof ( dark ) );
		DwmSetWindowAttribute ( window, 35, &caption, sizeof ( caption ) );
		DwmSetWindowAttribute ( window, 36, &text, sizeof ( text ) );

	}

	static void free_preview ( ) {

		preview.pixels.clear ( );
		preview.width = 0;
		preview.height = 0;

	}

	static void load_preview ( ) {

		wchar_t folder [ MAX_PATH ];

		free_preview ( );

		if ( selected >= 0 && selected < shelf->count ( ) && vm::folder_of ( shelf->at ( selected ).path, folder ) && !vm::read_preview ( folder, preview.pixels, preview.width, preview.height ) )
			free_preview ( );

	}

	static void rebuild ( ) {

		cards.clear ( );
		texts.clear ( );
		texts.fill ( static_cast < size_t > ( shelf->count ( ) ), card_text ( ) );

		for ( int32_t i = 0; i < shelf->count ( ); i++ ) {

			vm::entry& item = shelf->at ( i );
			card_text& text = texts [ i ];
			ui::machine_card card;
			const vm::preset* kind = vm::find_preset ( item.settings.os );

			vm::folder_of ( item.path, text.folder );
			card.name = item.readable ? item.settings.name : file_name ( item.path );
			card.mark = kind ? kind->mark : L"XT";
			card.modern = item.settings.machine == vm::MACHINE_PC;
			card.kind = card.modern ? L"Modern PC" : L"IBM PC/XT";
			card.memory = item.settings.memory;
			card.memory_mb = item.settings.memory_mb;
			card.processors = item.settings.processors;
			card.cap = item.settings.cap;
			card.iso = item.settings.iso;
			card.disk = item.settings.disk;

			WIN32_FILE_ATTRIBUTE_DATA disk_data;

			if ( card.modern && item.settings.disk [ 0 ] && GetFileAttributesExW ( item.settings.disk, GetFileExInfoStandard, &disk_data ) )
				card.disk_bytes = static_cast < uint64_t > ( disk_data.nFileSizeHigh ) << 32 | disk_data.nFileSizeLow;
			card.running = vm::is_running ( item.path );
			card.floppies [ 0 ] = item.settings.floppies [ 0 ];
			card.floppies [ 1 ] = item.settings.floppies [ 1 ];
			card.folder = text.folder;
			card.notes = item.settings.notes;

			if ( item.settings.custom_bios && item.settings.bios [ 0 ] )
				copy_text ( text.bios, file_name ( item.settings.bios ), MAX_PATH );
			else
				copy_text ( text.bios, L"Coldboot BIOS", MAX_PATH );

			card.bios = text.bios;

			if ( !item.readable ) {

				int32_t written = MultiByteToWideChar ( CP_UTF8, 0, item.problem, -1, text.problem, 119 );

				text.problem [ written > 0 ? written - 1 : 0 ] = 0;
				card.problem = text.problem;
				copy_text ( text.folder, item.path, MAX_PATH );

			}

			if ( i == selected && preview.width > 0 ) {

				card.preview = preview.pixels.data ( );
				card.preview_width = preview.width;
				card.preview_height = preview.height;

			}

			cards.push ( card );

		}

	}

	static void render ( ) {

		if ( !display || !view || !display->ready ( ) )
			return;

		RECT client;

		GetClientRect ( window, &client );

		float scale = dip_scale ( );

		display->set_dpi ( 96.0f * scale );
		display->resize ( client.right, client.bottom );
		view->layout ( static_cast < float > ( client.right ) / scale, static_cast < float > ( client.bottom ) / scale );

		ui::manager_view state;

		state.light = light;
		state.machines = cards.data ( );
		state.count = cards.count ( );
		state.selected = selected;
		state.presets = choices;
		state.preset_count = choice_count;
		state.location = home;
		state.host_memory_mb = host.memory_mb;
		state.host_logical = host.logical;
		state.host_physical = host.physical;
		view->paint ( *display, state, now_seconds ( ) );
		display->end ( );

		bool moving = view->animating ( now_seconds ( ) );

		if ( moving && !animating )
			SetTimer ( window, animation_timer, 16, nullptr );
		else if ( !moving && animating )
			KillTimer ( window, animation_timer );

		animating = moving;

	}

	static void select ( int32_t index ) {

		int32_t clamped = shelf->count ( ) ? base::clamp ( index, 0, shelf->count ( ) - 1 ) : -1;

		if ( clamped != selected ) {

			selected = clamped;
			load_preview ( );

		}

		rebuild ( );
		render ( );

	}

	static void toast ( const wchar_t* text ) {

		view->toast ( text, now_seconds ( ) );
		render ( );

	}

	static void unique_name ( const wchar_t* wanted, wchar_t* out ) {

		wchar_t safe [ vm::name_room ];
		int32_t length = 0;

		for ( ; wanted [ length ] && length < vm::name_room - 4; length++ ) {

			wchar_t c = wanted [ length ];
			bool bad = c < 32 || c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|';

			safe [ length ] = bad ? L'-' : c;

		}

		safe [ length ] = 0;

		for ( int32_t attempt = 1; attempt < 100; attempt++ ) {

			base::writer name;

			name.add ( safe );

			if ( attempt > 1 )
				name.add ( L" " ).number ( attempt );

			bool taken = false;

			for ( int32_t i = 0; i < shelf->count ( ) && !taken; i++ )
				taken = base::same_text ( shelf->at ( i ).settings.name, name.text ( ) );

			if ( !taken ) {

				copy_text ( out, name.text ( ), vm::name_room );

				return;

			}

		}

		copy_text ( out, safe, vm::name_room );

	}

	static void start_selected ( ) {

		if ( selected < 0 || !shelf->at ( selected ).readable )
			return;

		const wchar_t* path = shelf->at ( selected ).path;

		if ( vm::is_running ( path ) ) {

			HWND other = vm::find_window ( path );

			if ( other ) {

				ShowWindow ( other, IsIconic ( other ) ? SW_RESTORE : SW_SHOW );
				SetForegroundWindow ( other );

			}

			return;

		}

		if ( smoke ) {

			smoke_report->add ( L"start requested\r\n" );

			return;

		}

		wchar_t program [ MAX_PATH ];
		base::writer command;
		STARTUPINFOW startup = { sizeof ( STARTUPINFOW ) };
		PROCESS_INFORMATION process = { };

		GetModuleFileNameW ( nullptr, program, MAX_PATH );
		command.add ( L"\"" ).add ( program ).add ( L"\" --vm \"" ).add ( path ).add ( L"\"" );

		static wchar_t line [ base::writer::capacity ];

		copy_text ( line, command.text ( ), base::writer::capacity );

		if ( !CreateProcessW ( program, line, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process ) ) {

			toast ( L"Couldn't start the machine" );

			return;

		}

		CloseHandle ( process.hThread );
		CloseHandle ( process.hProcess );

	}

	static void open_new_as ( int32_t index, const wchar_t* image, const wchar_t* disk ) {

		int32_t count = 0;
		const vm::preset* table = vm::presets ( count );
		wchar_t name [ vm::name_room ];

		index = base::clamp ( index, 0, count - 1 );
		unique_name ( table [ index ].label, name );
		view->form = ui::machine_form ( );
		view->apply_preset ( index );
		view->form.name.set ( name );
		view->form.name.anchor = 0;

		wchar_t user [ 32 ];

		vm::default_user_name ( user, 32 );
		view->form.user.set ( user );

		if ( image )
			copy_text ( view->form.iso, image, ui::path_room );

		if ( disk ) {

			copy_text ( view->form.disk, disk, ui::path_room );
			view->form.disk_mode = 1;

		}

		view->subject_running = false;
		view->open_sheet ( ui::SHEET_NEW, now_seconds ( ) );
		render ( );

	}

	static void open_new ( ) {

		open_new_as ( preset_index ( "windows11" ), nullptr, nullptr );

	}

	static void open_image ( const wchar_t* path ) {

		int32_t count = 0;
		const vm::preset* table = vm::presets ( count );
		const vm::preset* kind = vm::detect_image ( path );

		open_new_as ( kind ? static_cast < int32_t > ( kind - table ) : preset_index ( "pc" ), path, nullptr );

	}

	static void form_to_config ( vm::config& out ) {

		const ui::machine_form& form = view->form;

		copy_text ( out.name, form.name.text, vm::name_room );
		out.machine = form.modern ? vm::MACHINE_PC : vm::MACHINE_XT;
		out.memory = form.memory;
		out.custom_bios = form.custom_bios && form.bios [ 0 ];
		copy_text ( out.bios, form.bios, MAX_PATH );
		copy_text ( out.floppies [ 0 ], form.floppies [ 0 ], MAX_PATH );
		copy_text ( out.floppies [ 1 ], form.floppies [ 1 ], MAX_PATH );
		out.memory_mb = form.memory_mb;
		out.processors = form.processors;
		out.cap = form.cap;
		out.disk_first = form.disk_first;
		out.uefi = form.uefi;
		out.network = form.network;
		out.clipboard = form.clipboard;
		out.file_sharing = form.file_sharing;
		copy_text ( out.iso, form.iso, MAX_PATH );
		copy_text ( out.disk, form.disk_mode == 1 || view->sheet == ui::SHEET_SETTINGS ? form.disk : L"", MAX_PATH );

	}

	static void create_machine ( ) {

		int32_t count = 0;
		const vm::preset* table = vm::presets ( count );
		const vm::preset& kind = table [ base::clamp ( view->form.preset, 0, count - 1 ) ];
		vm::config* settings = new vm::config ( );
		char error [ 160 ] = { };
		wchar_t path [ MAX_PATH ];

		form_to_config ( *settings );

		for ( int32_t i = 0; kind.id [ i ] && i < 23; i++ ) {

			settings->os [ i ] = kind.id [ i ];
			settings->os [ i + 1 ] = 0;

		}

		uint64_t disk_bytes = view->form.modern && view->form.disk_mode == 0 ? static_cast < uint64_t > ( view->form.disk_gb ) << 30 : 0;
		bool answering = view->asks_user ( ) && view->user_valid ( );

		settings->unattended = answering;

		bool made = shelf->create ( *settings, home, path, error, sizeof ( error ), disk_bytes );

		if ( made ) {

			base::writer note;
			wchar_t folder [ MAX_PATH ];

			note.add ( L"Created \x201C" ).add ( settings->name ).add ( L"\x201D" );

			if ( answering && vm::folder_of ( path, folder ) ) {

				vm::setup_answers* answers = new vm::setup_answers ( );
				wchar_t disc [ MAX_PATH ];
				char problem [ 160 ] = { };

				copy_text ( answers->user, view->form.user.text, 64 );
				copy_text ( answers->password, view->form.password.text, 64 );
				answers->uefi = view->form.uefi;
				answers->windows11 = base::same_text ( settings->os, "windows11" );
				vm::image_language ( view->form.iso, answers->language, sizeof ( answers->language ) );

				if ( !vm::write_setup_disc ( folder, *answers, disc, problem, sizeof ( problem ) ) )
					{

					note.clear ( );
					note.add ( L"Created the machine, but couldn't write its setup answers, so Windows Setup will ask its usual questions" );

				}

				delete answers;

			}
			view->close_sheet ( );
			select ( shelf->find ( path ) );
			toast ( note.text ( ) );

		} else {

			wchar_t text [ 160 ];
			int32_t written = MultiByteToWideChar ( CP_UTF8, 0, error, -1, text, 159 );

			text [ written > 0 ? written - 1 : 0 ] = 0;
			toast ( text );

		}

		delete settings;

	}

	static void open_settings ( ) {

		if ( selected < 0 || !shelf->at ( selected ).readable )
			return;

		const vm::config& source = shelf->at ( selected ).settings;

		view->form = ui::machine_form ( );
		view->form.preset = preset_index ( source.os );
		view->form.name.set ( source.name );
		view->form.memory = source.memory;
		view->form.custom_bios = source.custom_bios;
		copy_text ( view->form.bios, source.bios, ui::path_room );
		copy_text ( view->form.floppies [ 0 ], source.floppies [ 0 ], ui::path_room );
		copy_text ( view->form.floppies [ 1 ], source.floppies [ 1 ], ui::path_room );
		view->form.modern = source.machine == vm::MACHINE_PC;
		view->form.memory_mb = source.memory_mb;
		view->form.processors = source.processors;
		view->form.cap = source.cap;
		view->form.disk_first = source.disk_first;
		view->form.uefi = source.uefi;
		view->form.network = source.network;
		view->form.clipboard = source.clipboard;
		view->form.file_sharing = source.file_sharing;
		view->form.disk_mode = source.disk [ 0 ] ? 1 : 2;
		copy_text ( view->form.iso, source.iso, ui::path_room );
		copy_text ( view->form.disk, source.disk, ui::path_room );
		view->subject_running = vm::is_running ( shelf->at ( selected ).path );
		view->open_sheet ( ui::SHEET_SETTINGS, now_seconds ( ) );
		render ( );

	}

	static void save_machine ( ) {

		if ( selected < 0 )
			return;

		vm::entry& item = shelf->at ( selected );

		if ( vm::is_running ( item.path ) ) {

			toast ( L"Close the machine's window first" );

			return;

		}

		form_to_config ( item.settings );

		if ( !vm::save ( item.path, item.settings ) ) {

			toast ( L"Couldn't save the machine file" );
			shelf->refresh ( selected );

			return;

		}

		shelf->refresh ( selected );
		view->close_sheet ( );
		rebuild ( );
		toast ( L"Settings saved" );

	}

	static void ask_remove ( ) {

		if ( selected < 0 )
			return;

		vm::entry& item = shelf->at ( selected );

		copy_text ( view->subject, item.readable ? item.settings.name : file_name ( item.path ), 64 );
		view->subject_running = vm::is_running ( item.path );
		view->open_sheet ( ui::SHEET_REMOVE, now_seconds ( ) );
		render ( );

	}

	static void forget ( bool delete_files ) {

		if ( selected < 0 )
			return;

		vm::entry& item = shelf->at ( selected );

		if ( vm::is_running ( item.path ) ) {

			toast ( L"Close the machine's window first" );

			return;

		}

		if ( delete_files ) {

			shelf->refresh ( selected );

			if ( !vm::erase ( item.path, item.readable ? &item.settings : nullptr ) ) {

				toast ( L"Couldn't delete the machine's files" );

				return;

			}

		}

		int32_t index = selected;

		shelf->remove ( index );
		shelf->save ( );
		view->close_sheet ( );
		selected = -1;
		select ( index );
		toast ( delete_files ? L"Deleted" : L"Removed from the list" );

	}

	static void add_existing ( const wchar_t* chosen ) {

		wchar_t path [ MAX_PATH ];

		if ( chosen )
			copy_text ( path, chosen, MAX_PATH );
		else if ( !open_file ( window, FILE_MACHINE, path ) )
			return;

		int32_t found = shelf->find ( path );

		if ( found >= 0 ) {

			select ( found );
			toast ( L"That machine is already in the list" );

			return;

		}

		shelf->add ( path );
		shelf->save ( );
		select ( shelf->count ( ) - 1 );

		vm::entry& added = shelf->at ( shelf->count ( ) - 1 );

		if ( !added.readable )
			toast ( L"Added, but the file can't be opened" );
		else {

			base::writer note;

			note.add ( L"Added \x201C" ).add ( added.settings.name ).add ( L"\x201D" );
			toast ( note.text ( ) );

		}

	}

	static void act ( ui::manager_action action, int32_t index ) {

		switch ( action ) {

			case ui::ACTION_SELECT:
				select ( index );
				break;

			case ui::ACTION_START:
				start_selected ( );
				break;

			case ui::ACTION_NEW:
				open_new ( );
				break;

			case ui::ACTION_ADD:
				add_existing ( nullptr );
				break;

			case ui::ACTION_SETTINGS:
				open_settings ( );
				break;

			case ui::ACTION_REMOVE:
				ask_remove ( );
				break;

			case ui::ACTION_OPEN_FOLDER:

				if ( selected >= 0 && selected < texts.count ( ) && !smoke )
					ShellExecuteW ( window, L"open", texts [ selected ].folder, nullptr, nullptr, SW_SHOWNORMAL );

				break;

			case ui::ACTION_CREATE:
				create_machine ( );
				break;

			case ui::ACTION_SAVE:
				save_machine ( );
				break;

			case ui::ACTION_FORGET:
				forget ( false );
				break;

			case ui::ACTION_DELETE:
				forget ( true );
				break;

			case ui::ACTION_PICK_FLOPPY: {

				wchar_t path [ MAX_PATH ];

				if ( open_file ( window, FILE_DISK, path ) )
					copy_text ( view->form.floppies [ index & 1 ], path, ui::path_room );

				break;

			}

			case ui::ACTION_PICK_BIOS: {

				wchar_t path [ MAX_PATH ];

				if ( open_file ( window, FILE_ROM, path ) ) {

					copy_text ( view->form.bios, path, ui::path_room );
					view->form.custom_bios = true;

				}

				break;

			}

			case ui::ACTION_PICK_ISO: {

				wchar_t path [ MAX_PATH ];

				if ( open_file ( window, FILE_IMAGE, path ) )
					copy_text ( view->form.iso, path, ui::path_room );

				break;

			}

			case ui::ACTION_PICK_DISK: {

				wchar_t path [ MAX_PATH ];

				if ( open_file ( window, FILE_HARD_DISK, path ) ) {

					copy_text ( view->form.disk, path, ui::path_room );
					view->form.disk_mode = 1;

				}

				break;

			}

			default:
				break;

		}

		render ( );

	}

	static void migrate ( ) {

		if ( !prefs.disks [ 0 ] [ 0 ] && !prefs.disks [ 1 ] [ 0 ] && !prefs.custom_bios )
			return;

		vm::config* settings = new vm::config ( );
		char error [ 160 ] = { };

		unique_name ( L"IBM PC/XT", settings->name );
		settings->memory = base::clamp ( prefs.memory & ~15u, 64u, 640u );
		settings->turbo = prefs.turbo;
		settings->crt = prefs.crt;
		settings->fast_floppy = prefs.fast_floppy;
		settings->custom_bios = prefs.custom_bios;
		copy_text ( settings->bios, prefs.bios, MAX_PATH );
		copy_text ( settings->floppies [ 0 ], prefs.disks [ 0 ], MAX_PATH );
		copy_text ( settings->floppies [ 1 ], prefs.disks [ 1 ], MAX_PATH );
		shelf->create ( *settings, home, nullptr, error, sizeof ( error ) );
		delete settings;

	}

	static void store_placement ( ) {

		WINDOWPLACEMENT place = { sizeof ( WINDOWPLACEMENT ) };

		if ( smoke || !GetWindowPlacement ( window, &place ) )
			return;

		settings current;

		load_settings ( current );
		current.manager_x = place.rcNormalPosition.left;
		current.manager_y = place.rcNormalPosition.top;
		current.manager_width = place.rcNormalPosition.right - place.rcNormalPosition.left;
		current.manager_height = place.rcNormalPosition.bottom - place.rcNormalPosition.top;
		current.manager_maximized = place.showCmd == SW_SHOWMAXIMIZED;
		save_settings ( current );

	}

	static void refresh_running ( ) {

		bool changed = false;

		for ( int32_t i = 0; i < cards.count ( ) && i < shelf->count ( ); i++ ) {

			bool now = vm::is_running ( shelf->at ( i ).path );

			if ( now != cards [ i ].running ) {

				changed = true;

				if ( !now ) {

					shelf->refresh ( i );

					if ( i == selected )
						load_preview ( );

				}

			}

		}

		if ( changed ) {

			rebuild ( );
			render ( );

		}

	}

	static void smoke_capture ( const wchar_t* name ) {

		base::writer path;

		path.add ( smoke_folder ).add ( L"\\" ).add ( name );

		for ( int32_t i = 0; i < 20; i++ ) {

			render ( );
			Sleep ( 16 );

		}

		display->save ( path.text ( ) );

	}

	static void smoke_tick ( ) {

		smoke_step++;

		if ( smoke_step == 2 )
			smoke_capture ( L"manager_empty.png" );
		else if ( smoke_step == 3 ) {

			open_new ( );
			smoke_report->add ( L"default name " ).add ( view->form.name.text ).add ( view->name_valid ( ) ? L" valid\r\n" : L" invalid\r\n" );
			view->form.preset = 1;
			smoke_capture ( L"manager_new.png" );

		} else if ( smoke_step == 4 ) {

			act ( ui::ACTION_CREATE, 0 );
			smoke_report->add ( L"created " ).number ( shelf->count ( ) ).add ( L" " ).add ( selected >= 0 ? shelf->at ( selected ).settings.name : L"-" ).add ( L"\r\n" );
			smoke_capture ( L"manager_created.png" );

		} else if ( smoke_step == 5 ) {

			act ( ui::ACTION_SETTINGS, 0 );
			view->form.memory_mb = 3072;
			view->form.processors = 3;
			view->form.cap = 60;
			view->form.name.set ( L"Smoke PC" );
			act ( ui::ACTION_SAVE, 0 );

			vm::config reread;
			char error [ 160 ] = { };
			bool loaded = selected >= 0 && vm::load ( shelf->at ( selected ).path, reread, error, sizeof ( error ) );
			WIN32_FILE_ATTRIBUTE_DATA disk = { };
			bool sized = loaded && GetFileAttributesExW ( reread.disk, GetFileExInfoStandard, &disk ) && disk.nFileSizeHigh == 16;

			smoke_report->add ( L"saved " ).add ( loaded && reread.machine == vm::MACHINE_PC && reread.memory_mb == 3072 && reread.processors == 3 && reread.cap == 60 && base::same_text ( reread.name, L"Smoke PC" ) ? L"yes" : L"no" ).add ( L"\r\n" );
			smoke_report->add ( L"disk " ).add ( sized ? L"64 GB" : L"missing" ).add ( L"\r\n" );
			act ( ui::ACTION_START, 0 );

		} else if ( smoke_step == 6 ) {

			act ( ui::ACTION_REMOVE, 0 );
			smoke_capture ( L"manager_remove.png" );

		} else if ( smoke_step == 7 ) {

			wchar_t path [ MAX_PATH ];

			copy_text ( path, shelf->count ( ) ? shelf->at ( 0 ).path : L"", MAX_PATH );
			act ( ui::ACTION_FORGET, 0 );
			smoke_report->add ( L"forgot " ).add ( shelf->count ( ) == 0 ? L"yes" : L"no" ).add ( L"\r\n" );
			add_existing ( path );
			smoke_report->add ( L"added back " ).add ( shelf->count ( ) == 1 && shelf->at ( 0 ).readable ? L"yes" : L"no" ).add ( L"\r\n" );

			wchar_t folder [ MAX_PATH ];
			base::list < uint32_t > pattern;

			pattern.fill ( 320 * 200, 0 );

			for ( int32_t y = 0; y < 200; y++ ) {

				for ( int32_t x = 0; x < 320; x++ )
					pattern [ y * 320 + x ] = ( x / 40 + y / 25 ) & 1 ? 0x55FFFF : 0xFF55FF;

			}

			if ( shelf->count ( ) && vm::folder_of ( shelf->at ( 0 ).path, folder ) )
				vm::write_preview ( folder, pattern.data ( ), 320, 200 );

			selected = -1;
			select ( 0 );
			smoke_report->add ( L"preview " ).add ( preview.width == 320 && preview.height == 200 ? L"yes" : L"no" ).add ( L"\r\n" );
			smoke_capture ( L"manager_back.png" );

		} else if ( smoke_step == 8 ) {

			wchar_t path [ MAX_PATH ] = { };
			wchar_t disk [ MAX_PATH ] = { };
			wchar_t folder [ MAX_PATH ] = { };

			if ( shelf->count ( ) ) {

				copy_text ( path, shelf->at ( 0 ).path, MAX_PATH );
				copy_text ( disk, shelf->at ( 0 ).settings.disk, MAX_PATH );
				vm::folder_of ( path, folder );

			}

			act ( ui::ACTION_DELETE, 0 );

			bool gone = path [ 0 ] && disk [ 0 ] && shelf->count ( ) == 0 && GetFileAttributesW ( path ) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW ( disk ) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW ( folder ) == INVALID_FILE_ATTRIBUTES;

			smoke_report->add ( L"deleted " ).add ( gone ? L"yes" : L"no" ).add ( L"\r\n" );

		} else if ( smoke_step == 9 ) {

			char bytes [ 1024 ];
			int32_t size = WideCharToMultiByte ( CP_UTF8, 0, smoke_report->text ( ), smoke_report->size ( ), bytes, sizeof ( bytes ), nullptr, nullptr );
			base::writer path;

			path.add ( smoke_folder ).add ( L"\\manager.txt" );
			files::write ( path.text ( ), bytes, size );
			DestroyWindow ( window );

		}

	}

	static LRESULT __stdcall procedure ( HWND handle, UINT message, WPARAM wide, LPARAM extra ) {

		switch ( message ) {

			case WM_ERASEBKGND:
				return 1;

			case WM_PAINT:
				ValidateRect ( handle, nullptr );
				render ( );
				return 0;

			case WM_SIZE:
				render ( );
				return 0;

			case WM_DPICHANGED: {

				const RECT* suggested = reinterpret_cast < const RECT* > ( extra );

				SetWindowPos ( handle, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE );
				render ( );

				return 0;

			}

			case WM_GETMINMAXINFO: {

				MINMAXINFO* limits = reinterpret_cast < MINMAXINFO* > ( extra );
				float scale = static_cast < float > ( GetDpiForWindow ( handle ) ) / 96.0f;

				limits->ptMinTrackSize.x = static_cast < LONG > ( 900.0f * scale );
				limits->ptMinTrackSize.y = static_cast < LONG > ( 640.0f * scale );

				return 0;

			}

			case WM_TIMER:

				if ( wide == status_timer )
					refresh_running ( );
				else if ( wide == smoke_timer )
					smoke_tick ( );
				else
					render ( );

				return 0;

			case WM_MOUSEMOVE: {

				float scale = dip_scale ( );
				TRACKMOUSEEVENT track = { sizeof ( TRACKMOUSEEVENT ), TME_LEAVE, handle, 0 };

				TrackMouseEvent ( &track );
				view->mouse_move ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ) / scale, static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) / scale, now_seconds ( ) );
				render ( );

				return 0;

			}

			case WM_MOUSELEAVE:
				view->mouse_leave ( );
				render ( );
				return 0;

			case WM_LBUTTONDOWN: {

				float scale = dip_scale ( );

				SetCapture ( handle );
				view->mouse_down ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ) / scale, static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) / scale, now_seconds ( ) );
				render ( );

				return 0;

			}

			case WM_LBUTTONUP: {

				float scale = dip_scale ( );
				int32_t index = 0;
				bool twice = double_click;

				ReleaseCapture ( );
				double_click = false;

				ui::manager_action action = view->mouse_up ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ) / scale, static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) / scale, index );

				if ( twice && action == ui::ACTION_SELECT && view->sheet == ui::SHEET_NONE ) {

					select ( index );
					start_selected ( );

				} else
					act ( action, index );

				return 0;

			}

			case WM_LBUTTONDBLCLK: {

				float scale = dip_scale ( );

				SetCapture ( handle );
				double_click = true;
				view->mouse_down ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ) / scale, static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) / scale, now_seconds ( ) );
				render ( );

				return 0;

			}

			case WM_MOUSEWHEEL:
				view->wheel ( static_cast < float > ( GET_WHEEL_DELTA_WPARAM ( wide ) ) * 0.5f );
				render ( );
				return 0;

			case WM_KEYDOWN: {

				bool control = GetKeyState ( VK_CONTROL ) < 0;
				int32_t index = 0;

				if ( control && wide == 'V' && OpenClipboard ( handle ) ) {

					HANDLE data = GetClipboardData ( CF_UNICODETEXT );
					const wchar_t* text = data ? static_cast < const wchar_t* > ( GlobalLock ( data ) ) : nullptr;

					view->paste ( text );

					if ( text )
						GlobalUnlock ( data );

					CloseClipboard ( );
					render ( );

					return 0;

				}

				ui::manager_action action = view->key ( static_cast < uint32_t > ( wide ), GetKeyState ( VK_SHIFT ) < 0, control, index );

				if ( action == ui::ACTION_SELECT )
					select ( selected + index );
				else
					act ( action, index );

				return 0;

			}

			case WM_CHAR:
				view->character ( static_cast < wchar_t > ( wide ) );
				render ( );
				return 0;

			case WM_DROPFILES: {

				HDROP drop = reinterpret_cast < HDROP > ( wide );
				wchar_t path [ MAX_PATH ];
				UINT total = DragQueryFileW ( drop, 0xFFFFFFFF, nullptr, 0 );

				for ( UINT i = 0; i < total; i++ ) {

					if ( !DragQueryFileW ( drop, i, path, MAX_PATH ) )
						continue;

					if ( ends_with ( path, L".iso" ) )
						open_image ( path );
					else if ( ends_with ( path, L".img" ) || ends_with ( path, L".raw" ) )
						open_new_as ( preset_index ( "pc" ), nullptr, path );
					else if ( ends_with ( path, L".coldboot" ) )
						add_existing ( path );
					else
						toast ( L"Drop an ISO, a hard disk image or a .coldboot machine" );

				}

				DragFinish ( drop );

				return 0;

			}

			case WM_SETTINGCHANGE:

				if ( extra && base::same_text ( reinterpret_cast < const wchar_t* > ( extra ), L"ImmersiveColorSet" ) ) {

					apply_theme ( );
					render ( );

				}

				break;

			case WM_ACTIVATE:

				if ( LOWORD ( wide ) != WA_INACTIVE )
					refresh_running ( );

				break;

			case WM_CLOSE:
				DestroyWindow ( handle );
				return 0;

			case WM_DESTROY:
				store_placement ( );
				files::settle ( );
				PostQuitMessage ( 0 );
				return 0;

			default:
				break;

		}

		return DefWindowProcW ( handle, message, wide, extra );

	}

	int manager_main ( const wchar_t* smoke_path ) {

		QueryPerformanceFrequency ( &frequency );
		smoke = smoke_path != nullptr;

		if ( smoke )
			copy_text ( smoke_folder, smoke_path, MAX_PATH );
		else
			load_settings ( prefs );

		shelf = new vm::library ( );
		view = new ui::manager ( );
		display = new render::gpu ( );
		smoke_report = new base::writer ( );

		base::writer list;

		if ( smoke ) {

			list.add ( smoke_folder ).add ( L"\\library.list" );
			copy_text ( home, smoke_folder, MAX_PATH );

			base::writer machines;

			machines.add ( smoke_folder ).add ( L"\\machines" );
			copy_text ( home, machines.text ( ), MAX_PATH );

		} else {

			list.add ( data_path ( L"library.list" ) );

			if ( !vm::default_folder ( home ) )
				copy_text ( home, files::folder ( ), MAX_PATH );

		}

		bool first_run = GetFileAttributesW ( list.text ( ) ) == INVALID_FILE_ATTRIBUTES;

		shelf->load ( list.text ( ) );

		if ( first_run && !smoke )
			migrate ( );

		const vm::preset* table = vm::presets ( choice_count );

		host = vm::read_host ( );
		choice_count = base::min ( choice_count, 12 );

		for ( int32_t i = 0; i < choice_count; i++ ) {

			choices [ i ].label = table [ i ].label;
			choices [ i ].detail = table [ i ].detail;
			choices [ i ].mark = table [ i ].mark;
			choices [ i ].modern = table [ i ].machine == vm::MACHINE_PC;
			choices [ i ].memory_mb = table [ i ].memory;
			choices [ i ].processors = table [ i ].processors;
			choices [ i ].disk_gb = table [ i ].disk_gb;
			choices [ i ].uefi = base::same_text ( table [ i ].id, "windows11" );
			choices [ i ].windows = base::same_text ( table [ i ].id, "windows11" ) || base::same_text ( table [ i ].id, "windows10" );

		}

		HINSTANCE instance = GetModuleHandleW ( nullptr );
		WNDCLASSEXW type = { };

		type.cbSize = sizeof ( type );
		type.style = CS_DBLCLKS;
		type.lpfnWndProc = &procedure;
		type.hInstance = instance;
		type.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
		type.hIcon = LoadIconW ( instance, MAKEINTRESOURCEW ( 1 ) );
		type.lpszClassName = L"coldboot.manager";
		RegisterClassExW ( &type );

		window = CreateWindowExW ( 0, L"coldboot.manager", L"Coldboot", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760, nullptr, nullptr, instance, nullptr );

		if ( !window )
			return 1;

		float scale = dip_scale ( );
		RECT bounds = { 0, 0, static_cast < LONG > ( 1180.0f * scale ), static_cast < LONG > ( 760.0f * scale ) };
		RECT wanted = { prefs.manager_x, prefs.manager_y, prefs.manager_x + prefs.manager_width, prefs.manager_y + prefs.manager_height };

		AdjustWindowRectExForDpi ( &bounds, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow ( window ) );

		if ( !smoke && prefs.manager_width > 200 && prefs.manager_height > 200 && MonitorFromRect ( &wanted, MONITOR_DEFAULTTONULL ) )
			SetWindowPos ( window, nullptr, wanted.left, wanted.top, prefs.manager_width, prefs.manager_height, SWP_NOZORDER | SWP_NOACTIVATE );
		else
			SetWindowPos ( window, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );

		apply_theme ( );

		RECT client;

		GetClientRect ( window, &client );

		if ( !display->create ( window, client.right, client.bottom ) ) {

			if ( !smoke )
				MessageBoxW ( window, L"Coldboot needs Direct3D 11 and couldn't start it on this PC.", L"Coldboot", MB_ICONERROR );

			return 1;

		}

		display->set_dpi ( 96.0f * scale );
		selected = -1;
		select ( 0 );

		if ( smoke )
			SetTimer ( window, smoke_timer, 120, nullptr );
		else {

			DragAcceptFiles ( window, TRUE );
			SetTimer ( window, status_timer, 1000, nullptr );
			ShowWindow ( window, prefs.manager_maximized ? SW_SHOWMAXIMIZED : SW_SHOW );

		}

		MSG message;

		while ( GetMessageW ( &message, nullptr, 0, 0 ) > 0 ) {

			TranslateMessage ( &message );
			DispatchMessageW ( &message );

		}

		free_preview ( );
		delete display;
		delete view;
		delete shelf;
		delete smoke_report;

		return 0;

	}

}