#include "app/dialogs.hh"
#include "app/keys.hh"
#include "app/session.hh"
#include "app/settings.hh"
#include "base/files.hh"
#include "render/gpu.hh"
#include "storage/blank.hh"
#include "storage/program.hh"
#include "ui/shell.hh"
#include <windows.h>
#include <objbase.h>
#include <dwmapi.h>
#include <shellapi.h>

#ifdef COLDBOOT_DEBUG

bool test_main ( int32_t& result );

#endif

static constexpr UINT_PTR animation_timer = 1;
static constexpr UINT_PTR status_timer = 2;
static constexpr UINT_PTR smoke_timer = 3;

static app::session* emulator = nullptr;
static app::settings config;
static render::gpu* display = nullptr;
static ui::shell* frontend = nullptr;
static uint32_t* frame = nullptr;
static int32_t frame_width = 0;
static int32_t frame_height = 0;
static HWND main_window = nullptr;
static HHOOK keyboard_hook = nullptr;
static WINDOWPLACEMENT saved_place = { sizeof ( WINDOWPLACEMENT ) };
static bool fullscreen = false;
static bool light_mode = false;
static bool captured = false;
static bool tracking = false;
static bool held [ 128 ] = { };
static wchar_t drive_names [ 2 ] [ MAX_PATH ];
static FILETIME program_times [ 2 ] = { };
static LARGE_INTEGER clock_frequency;
static bool smoke = false;
static int32_t smoke_step = 0;
static double smoke_started = 0.0;
static wchar_t smoke_folder [ MAX_PATH ];
static const wchar_t* capture_target = nullptr;

static double seconds ( ) {

	LARGE_INTEGER now;

	QueryPerformanceCounter ( &now );

	return static_cast < double > ( now.QuadPart ) / static_cast < double > ( clock_frequency.QuadPart );

}

static float dip_scale ( ) {

	return static_cast < float > ( GetDpiForWindow ( main_window ) ) / 96.0f;

}

static const wchar_t* file_name ( const wchar_t* path ) {

	const wchar_t* name = path;

	for ( const wchar_t* at = path; *at; at++ ) {

		if ( *at == L'\\' || *at == L'/' )
			name = at + 1;

	}

	return name;

}

static void copy_path ( wchar_t* out, const wchar_t* path ) {

	int32_t length = 0;

	for ( ; path && path [ length ] && length < MAX_PATH - 1; length++ )
		out [ length ] = path [ length ];

	out [ length ] = 0;

}

static void store_settings ( ) {

	if ( !smoke )
		app::save_settings ( config );

}

static void apply_theme ( ) {

	light_mode = app::use_light ( config );

	BOOL dark = light_mode ? FALSE : TRUE;
	COLORREF caption = light_mode ? RGB ( 0xF8, 0xF8, 0xF6 ) : RGB ( 0x15, 0x16, 0x1A );
	COLORREF text = light_mode ? RGB ( 0x1A, 0x1B, 0x1E ) : RGB ( 0xEC, 0xED, 0xEF );

	DwmSetWindowAttribute ( main_window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof ( dark ) );
	DwmSetWindowAttribute ( main_window, 35, &caption, sizeof ( caption ) );
	DwmSetWindowAttribute ( main_window, 36, &text, sizeof ( text ) );

}

static void render_frame ( ) {

	if ( !display || !frontend || !display->ready ( ) )
		return;

	RECT client;

	GetClientRect ( main_window, &client );

	float scale = dip_scale ( );
	double now = seconds ( );

	display->set_dpi ( 96.0f * scale );
	display->resize ( client.right, client.bottom );
	frontend->layout ( static_cast < float > ( client.right ) / scale, static_cast < float > ( client.bottom ) / scale, fullscreen );

	int32_t width = 0;
	int32_t height = 0;
	bool powered = emulator->state == app::SESSION_RUNNING;

	if ( powered && emulator->latest ( frame, width, height ) ) {

		frame_width = width;
		frame_height = height;

	}

	ui::view state;

	state.pixels = powered && frame_width ? frame : nullptr;
	state.frame_width = frame_width;
	state.frame_height = frame_height;
	state.powered = powered;
	state.failed = emulator->state == app::SESSION_FAILED;
	state.paused = emulator->paused;
	state.turbo = config.turbo;
	state.crt = config.crt;
	state.light = light_mode;
	state.theme = config.theme;
	state.fast_floppy = config.fast_floppy;
	state.custom_bios = config.custom_bios;
	state.captured = captured;
	state.fullscreen = fullscreen;
	state.can_load = GetFileAttributesW ( app::data_path ( L"quick.state" ) ) != INVALID_FILE_ATTRIBUTES;
	state.post_code = emulator->post_code ( );
	state.speed = emulator->speed ( );
	state.memory = config.memory;
	state.bios_name = config.custom_bios && config.bios [ 0 ] ? file_name ( config.bios ) : L"Coldboot BIOS";
	state.problem = emulator->problem;

	for ( int32_t d = 0; d < 2; d++ ) {

		app::drive_info info;

		emulator->drive ( d, info );
		copy_path ( drive_names [ d ], info.name );
		state.drives [ d ].name = drive_names [ d ];
		state.drives [ d ].kilobytes = info.kilobytes;
		state.drives [ d ].present = info.present;
		state.drives [ d ].protect = info.protect;
		state.drives [ d ].light = info.light;
		state.drives [ d ].program = info.program;

	}

	frontend->paint ( *display, state, now );

	if ( capture_target ) {

		display->save ( capture_target );
		capture_target = nullptr;

	}

	display->end ( );

	if ( frontend->animating ( now ) )
		SetTimer ( main_window, animation_timer, 16, nullptr );
	else
		KillTimer ( main_window, animation_timer );

}

static void toast ( const wchar_t* text ) {

	frontend->toast ( text, seconds ( ) );
	render_frame ( );

}

static void power_on ( ) {

	for ( bool& key : held )
		key = false;

	emulator->power_on ( main_window, config );
	SetTimer ( main_window, status_timer, 250, nullptr );
	render_frame ( );

}

static void release_keys ( ) {

	for ( int32_t scan = 0; scan < 128; scan++ ) {

		if ( !held [ scan ] )
			continue;

		held [ scan ] = false;
		emulator->key ( static_cast < uint8_t > ( scan | 0x80 ) );

	}

}

static void set_capture ( bool value );

static bool host_key ( WPARAM key, bool down, bool extended ) {

	if ( key == VK_CONTROL && extended ) {

		if ( down )
			set_capture ( !captured );

		return true;

	}

	if ( key == VK_F11 ) {

		if ( down )
			PostMessageW ( main_window, WM_APP + 10, 0, 0 );

		return true;

	}

	if ( key == VK_F12 ) {

		if ( down )
			PostMessageW ( main_window, WM_APP + 11, 0, 0 );

		return true;

	}

	if ( key == VK_PAUSE ) {

		if ( down )
			PostMessageW ( main_window, WM_APP + 12, 0, 0 );

		return true;

	}

	return false;

}

static void guest_key ( WPARAM key, LPARAM extra, bool down ) {

	if ( emulator->state != app::SESSION_RUNNING )
		return;

	int32_t scan = app::xt_scan_code ( key, extra );

	if ( scan < 0 )
		return;

	held [ scan ] = down;
	emulator->key ( static_cast < uint8_t > ( down ? scan : scan | 0x80 ) );

}

static LRESULT __stdcall keyboard_filter ( int code, WPARAM message, LPARAM data ) {

	if ( code == HC_ACTION && captured && GetForegroundWindow ( ) == main_window ) {

		const KBDLLHOOKSTRUCT* info = reinterpret_cast < const KBDLLHOOKSTRUCT* > ( data );
		bool down = !( info->flags & LLKHF_UP );
		bool extended = ( info->flags & LLKHF_EXTENDED ) != 0;
		WPARAM key = info->vkCode;

		if ( key == VK_RCONTROL )
			key = VK_CONTROL;
		else if ( key == VK_LCONTROL )
			key = VK_CONTROL;
		else if ( key == VK_LSHIFT || key == VK_RSHIFT )
			key = VK_SHIFT;
		else if ( key == VK_LMENU || key == VK_RMENU )
			key = VK_MENU;

		if ( host_key ( key, down, extended ) )
			return 1;

		LPARAM extra = static_cast < LPARAM > ( info->scanCode & 0xFF ) << 16 | ( extended ? 1ll << 24 : 0 );

		guest_key ( key, extra, down );

		return 1;

	}

	return CallNextHookEx ( keyboard_hook, code, message, data );

}

static void set_capture ( bool value ) {

	if ( value == captured )
		return;

	captured = value && emulator->state == app::SESSION_RUNNING;

	if ( captured && !keyboard_hook )
		keyboard_hook = SetWindowsHookExW ( WH_KEYBOARD_LL, &keyboard_filter, GetModuleHandleW ( nullptr ), 0 );

	if ( !captured && keyboard_hook ) {

		UnhookWindowsHookEx ( keyboard_hook );
		keyboard_hook = nullptr;
		release_keys ( );

	}

	render_frame ( );

}

static void toggle_fullscreen ( ) {

	LONG_PTR style = GetWindowLongPtrW ( main_window, GWL_STYLE );

	if ( !fullscreen ) {

		MONITORINFO monitor = { sizeof ( MONITORINFO ) };

		GetWindowPlacement ( main_window, &saved_place );
		GetMonitorInfoW ( MonitorFromWindow ( main_window, MONITOR_DEFAULTTONEAREST ), &monitor );
		SetWindowLongPtrW ( main_window, GWL_STYLE, ( style & ~WS_OVERLAPPEDWINDOW ) | WS_POPUP );
		SetWindowPos ( main_window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top, monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER );
		fullscreen = true;
		frontend->popover = ui::POPOVER_NONE;
		toast ( L"Press F11 to leave fullscreen" );

	} else {

		SetWindowLongPtrW ( main_window, GWL_STYLE, ( style & ~WS_POPUP ) | WS_OVERLAPPEDWINDOW );
		SetWindowPlacement ( main_window, &saved_place );
		SetWindowPos ( main_window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER );
		fullscreen = false;
		render_frame ( );

	}

}

static void take_screenshot ( ) {

	wchar_t path [ MAX_PATH ];

	if ( emulator->state != app::SESSION_RUNNING || !frame_width ) {

		toast ( L"Power on first to take a screenshot" );

		return;

	}

	if ( app::screenshot_path ( path ) && app::save_screenshot ( frame, frame_width, frame_height, path ) )
		toast ( L"Screenshot saved to Pictures\\Coldboot" );
	else
		toast ( L"Couldn't save the screenshot" );

}

static bool written_at ( const wchar_t* path, FILETIME& out ) {

	WIN32_FILE_ATTRIBUTE_DATA data;

	if ( !GetFileAttributesExW ( path, GetFileExInfoStandard, &data ) )
		return false;

	out = data.ftLastWriteTime;

	return true;

}

static bool insert_disk ( int32_t drive, const wchar_t* path ) {

	if ( !emulator->insert ( drive, path ) ) {

		toast ( emulator->note [ 0 ] ? emulator->note : L"That file isn't a floppy image from 160 KB to 1.44 MB" );

		return false;

	}

	copy_path ( config.disks [ drive ], path );
	store_settings ( );

	if ( machine::is_program_path ( path ) ) {

		written_at ( path, program_times [ drive ] );

		if ( drive == 0 && emulator->state == app::SESSION_RUNNING )
			emulator->reset ( );

		toast ( emulator->note );

	}

	return true;

}

static void watch_programs ( ) {

	for ( int32_t d = 0; d < 2; d++ ) {

		wchar_t path [ MAX_PATH ];
		FILETIME written;

		copy_path ( path, emulator->disk_path ( d ) );

		if ( !machine::is_program_path ( path ) || !written_at ( path, written ) )
			continue;

		if ( !program_times [ d ].dwLowDateTime && !program_times [ d ].dwHighDateTime ) {

			program_times [ d ] = written;

			continue;

		}

		if ( CompareFileTime ( &written, &program_times [ d ] ) != 0 ) {

			program_times [ d ] = written;
			insert_disk ( d, path );

		}

	}

}

static void new_disk ( uint32_t size, const wchar_t* label ) {

	wchar_t path [ MAX_PATH ];
	base::writer suggested;

	suggested.add ( L"Blank " ).add ( label ).add ( L".img" );

	if ( !app::save_file ( main_window, app::FILE_DISK, suggested.text ( ), path ) )
		return;

	base::list < uint8_t > image;

	if ( !machine::make_blank_disk ( size, GetTickCount ( ) * 2654435761u, image ) || !files::write ( path, reinterpret_cast < const char* > ( image.data ( ) ), image.count ( ) ) ) {

		toast ( L"Couldn't create the disk image" );

		return;

	}

	app::drive_info first;

	emulator->drive ( 0, first );

	int32_t drive = first.present ? 1 : 0;

	if ( insert_disk ( drive, path ) )
		toast ( drive ? L"Blank disk created in drive B" : L"Blank disk created in drive A" );

}

static void set_option ( bool& option, bool value ) {

	option = value;
	store_settings ( );

}

static void act ( ui::control id ) {

	switch ( id ) {

		case ui::CONTROL_POWER:

			if ( emulator->state == app::SESSION_RUNNING ) {

				set_capture ( false );
				emulator->power_off ( );
				KillTimer ( main_window, status_timer );
				render_frame ( );

			} else
				power_on ( );

			return;

		case ui::CONTROL_START:
			power_on ( );
			return;

		case ui::CONTROL_RESET:
			emulator->reset ( );
			toast ( L"Reset" );
			return;

		case ui::CONTROL_PAUSE:
			emulator->set_paused ( !emulator->paused );
			render_frame ( );
			return;

		case ui::CONTROL_SAVE:
			toast ( emulator->save_state ( app::data_path ( L"quick.state" ) ) ? L"State saved" : L"Couldn't save the state" );
			return;

		case ui::CONTROL_LOAD:
			toast ( emulator->load_state ( app::data_path ( L"quick.state" ) ) ? L"State loaded" : L"Couldn't load the state" );
			return;

		case ui::CONTROL_SHOT:
			take_screenshot ( );
			return;

		case ui::CONTROL_SPEED:
		case ui::CONTROL_SPEED_REAL:
		case ui::CONTROL_SPEED_TURBO:
			set_option ( config.turbo, id == ui::CONTROL_SPEED ? !config.turbo : id == ui::CONTROL_SPEED_TURBO );
			emulator->set_turbo ( config.turbo );
			render_frame ( );
			return;

		case ui::CONTROL_FULLSCREEN:
			toggle_fullscreen ( );
			return;

		case ui::CONTROL_SCREEN:

			if ( !captured ) {

				set_capture ( true );
				toast ( L"Keyboard captured. Right Ctrl releases it." );

			}

			return;

		case ui::CONTROL_INSERT_A:
		case ui::CONTROL_INSERT_B: {

			wchar_t path [ MAX_PATH ];
			int32_t drive = id == ui::CONTROL_INSERT_B ? 1 : 0;

			if ( app::open_file ( main_window, app::FILE_DISK, path ) )
				insert_disk ( drive, path );

			render_frame ( );

			return;

		}

		case ui::CONTROL_EJECT_A:
		case ui::CONTROL_EJECT_B: {

			int32_t drive = id == ui::CONTROL_EJECT_B ? 1 : 0;

			emulator->eject ( drive );
			config.disks [ drive ] [ 0 ] = 0;
			store_settings ( );
			render_frame ( );

			return;

		}

		case ui::CONTROL_PROTECT_A:
		case ui::CONTROL_PROTECT_B: {

			int32_t drive = id == ui::CONTROL_PROTECT_B ? 1 : 0;
			app::drive_info info;

			emulator->drive ( drive, info );
			emulator->set_protect ( drive, !info.protect );
			render_frame ( );

			return;

		}

		case ui::CONTROL_NEW_360:
			new_disk ( 368640, L"360 KB" );
			return;

		case ui::CONTROL_NEW_720:
			new_disk ( 737280, L"720 KB" );
			return;

		case ui::CONTROL_NEW_1200:
			new_disk ( 1228800, L"1.2 MB" );
			return;

		case ui::CONTROL_NEW_1440:
			new_disk ( 1474560, L"1.44 MB" );
			return;

		case ui::CONTROL_DISPLAY_SHARP:
		case ui::CONTROL_DISPLAY_CRT:
			set_option ( config.crt, id == ui::CONTROL_DISPLAY_CRT );
			render_frame ( );
			return;

		case ui::CONTROL_THEME_SYSTEM:
		case ui::CONTROL_THEME_DARK:
		case ui::CONTROL_THEME_LIGHT:
			config.theme = id == ui::CONTROL_THEME_SYSTEM ? app::THEME_SYSTEM : ( id == ui::CONTROL_THEME_DARK ? app::THEME_DARK : app::THEME_LIGHT );
			store_settings ( );
			apply_theme ( );
			render_frame ( );
			return;

		case ui::CONTROL_FLOPPY_REAL:
		case ui::CONTROL_FLOPPY_FAST:
			set_option ( config.fast_floppy, id == ui::CONTROL_FLOPPY_FAST );
			emulator->set_fast_floppy ( config.fast_floppy );
			render_frame ( );
			return;

		case ui::CONTROL_MEMORY_256:
		case ui::CONTROL_MEMORY_512:
		case ui::CONTROL_MEMORY_640:
			config.memory = id == ui::CONTROL_MEMORY_256 ? 256 : ( id == ui::CONTROL_MEMORY_512 ? 512 : 640 );
			store_settings ( );
			render_frame ( );
			return;

		case ui::CONTROL_BIOS_BUILTIN:
			set_option ( config.custom_bios, false );
			render_frame ( );
			return;

		case ui::CONTROL_BIOS_CUSTOM: {

			wchar_t path [ MAX_PATH ];

			if ( app::open_file ( main_window, app::FILE_ROM, path ) ) {

				copy_path ( config.bios, path );
				set_option ( config.custom_bios, true );

			}

			render_frame ( );

			return;

		}

		default:
			return;

	}

}

static void smoke_capture ( const wchar_t* name ) {

	static wchar_t path [ MAX_PATH ];
	base::writer text;

	text.add ( smoke_folder ).add ( L"\\" ).add ( name );
	copy_path ( path, text.text ( ) );
	capture_target = path;
	render_frame ( );

}

static void smoke_tick ( ) {

	double elapsed = seconds ( ) - smoke_started;

	if ( smoke_step == 0 && elapsed >= 5.0 ) {

		smoke_capture ( L"smoke_running.png" );
		smoke_step = 1;

	} else if ( smoke_step == 1 && elapsed >= 5.4 ) {

		frontend->open_popover ( ui::POPOVER_DISKS, seconds ( ) );
		render_frame ( );
		smoke_step = 2;

	} else if ( smoke_step == 2 && elapsed >= 6.0 ) {

		smoke_capture ( L"smoke_disks.png" );
		smoke_step = 3;

	} else if ( smoke_step == 3 && elapsed >= 6.2 ) {

		base::writer state_path;
		base::writer report;

		state_path.add ( smoke_folder ).add ( L"\\smoke.state" );

		bool saved = emulator->save_state ( state_path.text ( ) );
		bool loaded = saved && emulator->load_state ( state_path.text ( ) );

		report.add ( L"speed " ).fixed ( emulator->speed ( ), 2 ).add ( L" MHz\r\nsaved " ).add ( saved ? L"yes" : L"no" ).add ( L"\r\nloaded " ).add ( loaded ? L"yes" : L"no" ).add ( L"\r\npost " ).number ( emulator->post_code ( ) ).add ( L"\r\ntheme " ).add ( light_mode ? L"light" : L"dark" ).add ( L"\r\n" );

		char bytes [ 512 ];
		int32_t size = WideCharToMultiByte ( CP_UTF8, 0, report.text ( ), report.size ( ), bytes, sizeof ( bytes ), nullptr, nullptr );
		base::writer report_path;

		report_path.add ( smoke_folder ).add ( L"\\smoke.txt" );
		files::write ( report_path.text ( ), bytes, size );
		frontend->popover = ui::POPOVER_NONE;
		config.theme = app::THEME_LIGHT;
		apply_theme ( );
		frontend->toast ( L"State saved", seconds ( ) );
		smoke_step = 4;

	} else if ( smoke_step == 4 && elapsed >= 6.6 ) {

		smoke_capture ( L"smoke_light.png" );
		smoke_step = 5;

	} else if ( smoke_step == 5 && elapsed >= 6.8 )
		DestroyWindow ( main_window );

}

static bool is_disk ( const wchar_t* path ) {

	WIN32_FILE_ATTRIBUTE_DATA data;

	if ( !GetFileAttributesExW ( path, GetFileExInfoStandard, &data ) || ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) || data.nFileSizeHigh )
		return false;

	return machine::is_program_path ( path ) || machine::floppy::identify ( data.nFileSizeLow ) != nullptr;

}

static void open_files ( const wchar_t* const* paths, int32_t count, int32_t first_drive ) {

	int32_t placed = 0;
	bool rom = false;

	for ( int32_t i = 0; i < count; i++ ) {

		if ( is_disk ( paths [ i ] ) ) {

			if ( placed < 2 && insert_disk ( ( first_drive + placed ) % 2, paths [ i ] ) )
				placed++;

		} else {

			copy_path ( config.bios, paths [ i ] );
			config.custom_bios = true;
			rom = true;

		}

	}

	if ( rom ) {

		store_settings ( );
		power_on ( );

	} else if ( placed && emulator->state != app::SESSION_RUNNING )
		power_on ( );
	else
		render_frame ( );

}

static LRESULT __stdcall procedure ( HWND window, UINT message, WPARAM wide, LPARAM extra ) {

	switch ( message ) {

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
			ValidateRect ( window, nullptr );
			render_frame ( );
			return 0;

		case WM_SIZE:
			render_frame ( );
			return 0;

		case WM_DPICHANGED: {

			const RECT* suggested = reinterpret_cast < const RECT* > ( extra );

			SetWindowPos ( window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE );
			render_frame ( );

			return 0;

		}

		case WM_GETMINMAXINFO: {

			MINMAXINFO* limits = reinterpret_cast < MINMAXINFO* > ( extra );
			float scale = static_cast < float > ( GetDpiForWindow ( window ) ) / 96.0f;

			limits->ptMinTrackSize.x = static_cast < LONG > ( 720.0f * scale );
			limits->ptMinTrackSize.y = static_cast < LONG > ( 560.0f * scale );

			return 0;

		}

		case WM_TIMER:

			if ( wide == smoke_timer )
				smoke_tick ( );
			else if ( wide == status_timer ) {

				watch_programs ( );
				render_frame ( );

			} else
				render_frame ( );

			return 0;

		case app::session::frame_message:
		case app::session::light_message:
			render_frame ( );
			return 0;

		case WM_APP + 10:
			toggle_fullscreen ( );
			return 0;

		case WM_APP + 11:
			take_screenshot ( );
			return 0;

		case WM_APP + 12:

			if ( emulator->state == app::SESSION_RUNNING ) {

				emulator->set_paused ( !emulator->paused );
				toast ( emulator->paused ? L"Paused" : L"Running" );

			}

			return 0;

		case WM_MOUSEMOVE: {

			if ( !tracking ) {

				TRACKMOUSEEVENT track = { sizeof ( TRACKMOUSEEVENT ), TME_LEAVE, window, 0 };

				TrackMouseEvent ( &track );
				tracking = true;

			}

			float scale = dip_scale ( );

			frontend->mouse_move ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ) / scale, static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) / scale, seconds ( ) );
			render_frame ( );

			return 0;

		}

		case WM_MOUSELEAVE:
			tracking = false;
			frontend->mouse_leave ( );
			render_frame ( );
			return 0;

		case WM_LBUTTONDOWN: {

			float scale = dip_scale ( );

			SetCapture ( window );
			frontend->mouse_down ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ) / scale, static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) / scale );
			render_frame ( );

			return 0;

		}

		case WM_LBUTTONUP: {

			float scale = dip_scale ( );

			ReleaseCapture ( );

			ui::control id = frontend->mouse_up ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ) / scale, static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) / scale );

			render_frame ( );
			act ( id );

			return 0;

		}

		case WM_DROPFILES: {

			HDROP drop = reinterpret_cast < HDROP > ( wide );
			static wchar_t names [ 4 ] [ MAX_PATH ];
			const wchar_t* paths [ 4 ];
			int32_t count = 0;
			UINT total = DragQueryFileW ( drop, 0xFFFFFFFF, nullptr, 0 );

			for ( UINT i = 0; i < total && count < 4; i++ ) {

				if ( DragQueryFileW ( drop, i, names [ count ], MAX_PATH ) ) {

					paths [ count ] = names [ count ];
					count++;

				}

			}

			DragFinish ( drop );
			open_files ( paths, count, GetAsyncKeyState ( VK_SHIFT ) & 0x8000 ? 1 : 0 );

			return 0;

		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP: {

			bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
			bool extended = ( extra >> 24 & 1 ) != 0;

			if ( message == WM_SYSKEYDOWN && wide == VK_F4 )
				break;

			if ( host_key ( wide, down, extended ) )
				return 0;

			guest_key ( wide, extra, down );

			return 0;

		}

		case WM_SYSCHAR:
		case WM_CHAR:
			return 0;

		case WM_SETTINGCHANGE:

			if ( extra && base::same_text ( reinterpret_cast < const wchar_t* > ( extra ), L"ImmersiveColorSet" ) ) {

				apply_theme ( );
				render_frame ( );

			}

			break;

		case WM_ACTIVATE:

			if ( LOWORD ( wide ) == WA_INACTIVE ) {

				set_capture ( false );
				release_keys ( );

			}

			break;

		case WM_CLOSE:
			DestroyWindow ( window );
			return 0;

		case WM_DESTROY: {

			WINDOWPLACEMENT place = { sizeof ( WINDOWPLACEMENT ) };

			if ( fullscreen )
				place = saved_place;
			else
				GetWindowPlacement ( window, &place );

			config.window_x = place.rcNormalPosition.left;
			config.window_y = place.rcNormalPosition.top;
			config.window_width = place.rcNormalPosition.right - place.rcNormalPosition.left;
			config.window_height = place.rcNormalPosition.bottom - place.rcNormalPosition.top;
			config.maximized = place.showCmd == SW_SHOWMAXIMIZED;
			store_settings ( );
			set_capture ( false );
			emulator->power_off ( );
			files::settle ( );
			PostQuitMessage ( 0 );

			return 0;

		}

		default:
			break;

	}

	return DefWindowProcW ( window, message, wide, extra );

}

static void open_command_line ( ) {

	int32_t count = 0;
	wchar_t** arguments = CommandLineToArgvW ( GetCommandLineW ( ), &count );
	const wchar_t* paths [ 4 ];
	int32_t found = 0;

	for ( int32_t i = 1; arguments && i < count && found < 4; i++ ) {

		if ( arguments [ i ] [ 0 ] != L'-' )
			paths [ found++ ] = arguments [ i ];

	}

	for ( int32_t i = 0; i < found; i++ ) {

		if ( is_disk ( paths [ i ] ) )
			continue;

		copy_path ( config.bios, paths [ i ] );
		config.custom_bios = true;

	}

	int32_t placed = 0;

	for ( int32_t i = 0; i < found; i++ ) {

		if ( is_disk ( paths [ i ] ) && placed < 2 && emulator->insert ( placed, paths [ i ] ) ) {

			copy_path ( config.disks [ placed ], paths [ i ] );
			placed++;

		}

	}

	if ( arguments )
		LocalFree ( arguments );

}

int program_main ( ) {

#ifdef COLDBOOT_DEBUG

	int32_t tested = 0;

	if ( test_main ( tested ) )
		return tested;

#endif

	SetProcessDpiAwarenessContext ( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
	CoInitializeEx ( nullptr, COINIT_APARTMENTTHREADED );
	QueryPerformanceFrequency ( &clock_frequency );

	const wchar_t* smoke_disk = nullptr;

#ifdef COLDBOOT_DEBUG

	int32_t argument_count = 0;
	wchar_t** arguments = CommandLineToArgvW ( GetCommandLineW ( ), &argument_count );

	for ( int32_t i = 1; arguments && i + 1 < argument_count; i++ ) {

		if ( base::same_text ( arguments [ i ], L"--smoke" ) ) {

			smoke = true;
			copy_path ( smoke_folder, arguments [ i + 1 ] );

			if ( i + 2 < argument_count )
				smoke_disk = arguments [ i + 2 ];

		}

	}

	static wchar_t smoke_disk_path [ MAX_PATH ];

	if ( smoke_disk ) {

		copy_path ( smoke_disk_path, smoke_disk );
		smoke_disk = smoke_disk_path;

	}

	if ( arguments )
		LocalFree ( arguments );

#endif

	if ( smoke )
		config.turbo = true;
	else
		app::load_settings ( config );

	light_mode = app::use_light ( config );

	HINSTANCE instance = GetModuleHandleW ( nullptr );
	WNDCLASSEXW type = { };

	type.cbSize = sizeof ( type );
	type.lpfnWndProc = &procedure;
	type.hInstance = instance;
	type.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
	type.hIcon = LoadIconW ( instance, MAKEINTRESOURCEW ( 1 ) );
	type.lpszClassName = L"coldboot";
	RegisterClassExW ( &type );

	emulator = new app::session ( );
	frontend = new ui::shell ( );
	display = new render::gpu ( );
	frame = static_cast < uint32_t* > ( runtime_allocate_zero ( sizeof ( uint32_t ) * machine::cga::max_width * machine::cga::max_height ) );

	emulator->muted = smoke;

	if ( smoke ) {

		if ( smoke_disk )
			emulator->insert ( 0, smoke_disk );

	} else {

		for ( int32_t d = 0; d < 2; d++ ) {

			if ( config.disks [ d ] [ 0 ] && !emulator->insert ( d, config.disks [ d ] ) )
				config.disks [ d ] [ 0 ] = 0;

		}

		open_command_line ( );

	}

	main_window = CreateWindowExW ( 0, L"coldboot", L"Coldboot", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 900, nullptr, nullptr, instance, nullptr );

	if ( !main_window )
		return 1;

	float scale = dip_scale ( );
	RECT bounds = { 0, 0, static_cast < LONG > ( 1040.0f * scale ), static_cast < LONG > ( 860.0f * scale ) };

	AdjustWindowRectExForDpi ( &bounds, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow ( main_window ) );

	RECT wanted = { config.window_x, config.window_y, config.window_x + config.window_width, config.window_y + config.window_height };

	if ( !smoke && config.window_width > 200 && config.window_height > 200 && MonitorFromRect ( &wanted, MONITOR_DEFAULTTONULL ) )
		SetWindowPos ( main_window, nullptr, wanted.left, wanted.top, config.window_width, config.window_height, SWP_NOZORDER | SWP_NOACTIVATE );
	else
		SetWindowPos ( main_window, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );

	apply_theme ( );

	RECT client;

	GetClientRect ( main_window, &client );

	if ( !display->create ( main_window, client.right, client.bottom ) ) {

		MessageBoxW ( main_window, L"Coldboot needs Direct3D 11 and couldn't start it on this PC.", L"Coldboot", MB_ICONERROR );

		return 1;

	}

	display->set_dpi ( 96.0f * scale );

	if ( smoke ) {

		smoke_started = seconds ( );
		SetTimer ( main_window, smoke_timer, 50, nullptr );

	} else {

		DragAcceptFiles ( main_window, TRUE );
		ShowWindow ( main_window, config.maximized ? SW_SHOWMAXIMIZED : SW_SHOW );

	}

	power_on ( );

	MSG message;

	while ( GetMessageW ( &message, nullptr, 0, 0 ) > 0 ) {

		TranslateMessage ( &message );
		DispatchMessageW ( &message );

	}

	delete emulator;
	delete display;
	delete frontend;
	runtime_release ( frame );

	return 0;

}