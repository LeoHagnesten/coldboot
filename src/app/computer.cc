#include "app/audio.hh"
#include "app/clipboard.hh"
#include "app/dialogs.hh"
#include "app/settings.hh"
#include "base/files.hh"
#include "base/gif.hh"
#include "base/png.hh"
#include "pc/computer.hh"
#include "pc/iso.hh"
#include "render/gpu.hh"
#include "ui/console.hh"
#include "vm/library.hh"
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

namespace app {

	static constexpr UINT_PTR smoke_timer = 2;

	struct speaker_link : pc::sound_sink {

		audio* output = nullptr;

		void play ( const float* frames, uint32_t count, uint32_t rate ) override {

			if ( output && output->rate )
				output->push_stereo ( frames, static_cast < int32_t > ( count ), rate );

		}

	};

	static pc::computer* machine = nullptr;
	static speaker_link speaker;
	static ui::console* frontend = nullptr;
	static render::gpu* display = nullptr;
	static vm::config* file = nullptr;
	static vm::config* run = nullptr;
	static const wchar_t* file_path = nullptr;
	static const settings* global = nullptr;
	static HWND window = nullptr;
	static HHOOK keyboard_hook = nullptr;
	static uint32_t* frame = nullptr;
	static int32_t frame_width = 0;
	static int32_t frame_height = 0;
	static bool running = false;
	static bool failed = false;
	static bool captured = false;
	static bool integrated = false;
	static bool integration_told = false;
	static uint8_t pointer_buttons = 0;
	static int32_t pointer_x = 0;
	static int32_t pointer_y = 0;
	static bool fullscreen = false;
	static bool light = false;
	static bool smoke = false;
	static bool host_down = false;
	static bool host_used = false;
	static bool held [ 2 ] [ 128 ] = { };
	static uint8_t buttons = 0;
	static long disc_seen = 0;
	static long disk_seen = 0;
	static double disc_until = 0.0;
	static double disk_until = 0.0;
	static double smoke_started = 0.0;
	static int32_t smoke_step = 0;
	static wchar_t smoke_folder [ MAX_PATH ] = { };
	static wchar_t problem [ 256 ] = { };
	static wchar_t disc_name [ MAX_PATH ] = { };
	static wchar_t disk_name [ MAX_PATH ] = { };
	static WINDOWPLACEMENT saved_place = { sizeof ( WINDOWPLACEMENT ) };
	static LARGE_INTEGER frequency;
	static constexpr int32_t sent_room = 64;
	static wchar_t ( *sent ) [ MAX_PATH ] = nullptr;
	static int32_t sent_count = 0;
	static int32_t sent_flip = 0;
	static wchar_t sent_disc [ MAX_PATH ] = { };
	static HANDLE sender = nullptr;
	static HANDLE typist = nullptr;
	static wchar_t* typed = nullptr;
	static volatile long typing_quit = 0;
	static wchar_t clipboard_inbox [ MAX_PATH ] = { };
	static DWORD ignore_sequence = 0;
	static bool tools_linked = false;
	static HANDLE pacer = nullptr;
	static double next_frame = 0.0;
	static double last_present = 0.0;
	static double fps_since = 0.0;
	static uint64_t shown_changes = ~0ull;
	static uint64_t counted_changes = 0;
	static int32_t fps_frames = 0;
	static int32_t fps_shown = -1;
	static bool input_seen = true;
	static bool slot_owed = false;
	static bool lights_shown [ 2 ] = { };
	static double refresh_period = 1.0 / 60.0;
	static gif::writer* recorder = nullptr;
	static wchar_t record_path [ MAX_PATH ] = { };
	static double record_seconds = 0.0;
	static double record_from = 0.0;
	static double record_started = 0.0;
	static bool shot_due = false;
	static bool shot_taken = false;
	static double record_next = 0.0;
	static double ask_seen = 0.0;

	struct send_job {

		wchar_t paths [ sent_room ] [ MAX_PATH ];
		int32_t count = 0;
		int32_t files = 0;
		wchar_t target [ MAX_PATH ] = { };
		bool ok = false;

	};

	struct ask_item {

		uint32_t id = 0;
		uint8_t protocol = 0;
		uint16_t guest_port = 0;
		uint32_t remote = 0;
		uint16_t remote_port = 0;
		wchar_t host [ 256 ] = { };
		wchar_t app [ 64 ] = { };
		bool named = false;
		double since = 0.0;

	};

	struct app_rule {

		wchar_t app [ 64 ] = { };
		bool allow = false;
		bool saved = false;

	};

	static base::list < ask_item > asks;
	static base::list < app_rule > rules;
	static bool ask_remember = false;
	static wchar_t ask_target [ 320 ] = { };

	static const ask_item* waiting_ask ( int32_t& others ) {

		const ask_item* first = nullptr;

		others = 0;

		for ( const ask_item& item : asks ) {

			if ( !item.named )
				continue;

			if ( !first )
				first = &item;
			else
				others++;

		}

		return first;

	}

	static double now_seconds ( ) {

		LARGE_INTEGER counter;

		QueryPerformanceCounter ( &counter );

		return static_cast < double > ( counter.QuadPart ) / static_cast < double > ( frequency.QuadPart );

	}

	static float scale ( ) {

		return static_cast < float > ( GetDpiForWindow ( window ) ) / 96.0f;

	}

	static void copy_wide ( wchar_t* out, const wchar_t* text, int32_t room ) {

		int32_t length = 0;

		for ( ; text && text [ length ] && length < room - 1; length++ )
			out [ length ] = text [ length ];

		out [ length ] = 0;

	}

	static const wchar_t* leaf ( const wchar_t* path ) {

		const wchar_t* name = path;

		for ( const wchar_t* at = path; *at; at++ ) {

			if ( *at == L'\\' || *at == L'/' )
				name = at + 1;

		}

		return name;

	}

	static void save_machine ( ) {

		if ( !smoke )
			vm::save ( file_path, *file );

	}

	static void apply_theme ( ) {

		light = use_light ( *global );

		BOOL dark = light ? FALSE : TRUE;
		COLORREF caption = light ? RGB ( 0xEE, 0xEE, 0xEC ) : RGB ( 0x17, 0x18, 0x1B );
		COLORREF text = light ? RGB ( 0x1B, 0x1C, 0x1F ) : RGB ( 0xE4, 0xE5, 0xE7 );

		DwmSetWindowAttribute ( window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof ( dark ) );
		DwmSetWindowAttribute ( window, 35, &caption, sizeof ( caption ) );
		DwmSetWindowAttribute ( window, 36, &text, sizeof ( text ) );

	}

	static void capture ( double now ) {

		if ( !running )
			return;

		int32_t width = 0;
		int32_t height = 0;

		if ( machine->screen ( frame, width, height, now ) ) {

			frame_width = width;
			frame_height = height;

		}

		long disc_count = machine->disc.activity;
		long disk_count = machine->drive.activity;

		if ( disc_count != disc_seen ) {

			disc_seen = disc_count;
			disc_until = now + 0.12;

		}

		if ( disk_count != disk_seen ) {

			disk_seen = disk_count;
			disk_until = now + 0.12;

		}

		uint64_t changes = machine->screen_changes ( );

		if ( changes != counted_changes ) {

			counted_changes = changes;
			fps_frames++;

		}

		if ( now - fps_since >= 1.0 ) {

			fps_shown = static_cast < int32_t > ( static_cast < double > ( fps_frames ) / ( now - fps_since ) + 0.5 );
			fps_frames = 0;
			fps_since = now;

		}

	}

	static void paint ( double now ) {

		if ( !display || !display->ready ( ) )
			return;

		RECT client;
		float factor = scale ( );
		uint64_t changes = running ? machine->screen_changes ( ) : 0;

		GetClientRect ( window, &client );
		display->set_dpi ( 96.0f * factor );
		display->resize ( client.right, client.bottom );
		frontend->layout ( static_cast < float > ( client.right ) / factor, static_cast < float > ( client.bottom ) / factor, fullscreen );

		ui::console_view view;

		view.fresh = changes != shown_changes;
		view.smooth = run->smooth;
		view.scaling = run->scaling;
		view.fps = run->frame_counter ? fps_shown : -1;
		view.private_run = vm::private_run ( *run );

		int32_t others = 0;
		const ask_item* asking = running ? waiting_ask ( others ) : nullptr;

		if ( asking ) {

			base::writer target;
			wchar_t dotted [ 20 ];

			if ( asking->host [ 0 ] )
				target.add ( asking->host );
			else {

				base::writer address;

				address.number ( static_cast < int32_t > ( asking->remote >> 24 ) ).add ( L"." ).number ( static_cast < int32_t > ( asking->remote >> 16 & 255 ) ).add ( L"." ).number ( static_cast < int32_t > ( asking->remote >> 8 & 255 ) ).add ( L"." ).number ( static_cast < int32_t > ( asking->remote & 255 ) );
				copy_wide ( dotted, address.text ( ), 20 );
				target.add ( dotted );

			}

			target.add ( L", port " ).number ( asking->remote_port ).add ( asking->protocol == 17 ? L" over UDP" : L"" );
			copy_wide ( ask_target, target.text ( ), 320 );
			view.ask_app = asking->app;
			view.ask_target = ask_target;
			view.ask_more = others;
			view.ask_remember = ask_remember;

		}

		shown_changes = changes;
		lights_shown [ 0 ] = now < disc_until;
		lights_shown [ 1 ] = now < disk_until;
		last_present = now;
		input_seen = false;
		slot_owed = false;

		view.light = light;
		view.running = running;
		view.failed = failed;
		view.captured = captured;
		view.integrated = integrated && running;
		view.tools_linked = tools_linked && running;
		view.fullscreen = fullscreen;
		view.pixels = running && frame_width ? frame : nullptr;
		view.frame_width = frame_width;
		view.frame_height = frame_height;
		view.problem = problem;
		view.disc = disc_name;
		view.disk = disk_name;
		view.processors = file->processors;
		view.memory_mb = file->memory_mb;
		view.disc_light = now < disc_until;
		view.disk_light = now < disk_until;
		frontend->paint ( *display, view, now );

		if ( shot_due ) {

			display->save ( record_path );
			shot_due = false;
			shot_taken = true;

		}

		if ( recorder && now >= record_next ) {

			base::list < uint32_t > grabbed;
			int32_t across = 0;
			int32_t down = 0;

			if ( display->read ( grabbed, across, down ) )
				recorder->frame ( grabbed.data ( ), across, down, 10, true );

			record_next = now + 0.1;

		}

		display->end ( );

	}

	static void render ( ) {

		double now = now_seconds ( );

		capture ( now );
		shown_changes = ~0ull;
		paint ( now );

	}

	static bool frame_wanted ( double now ) {

		bool lights [ 2 ] = { now < disc_until, now < disk_until };

		if ( recorder && now >= record_next )
			return true;

		return machine->screen_changes ( ) != shown_changes || input_seen || frontend->animating ( now ) || lights [ 0 ] != lights_shown [ 0 ] || lights [ 1 ] != lights_shown [ 1 ] || now - last_present > 0.5;

	}

	static double read_refresh ( ) {

		MONITORINFOEXW monitor = { };
		DEVMODEW mode = { };

		monitor.cbSize = sizeof ( monitor );
		mode.dmSize = sizeof ( mode );

		if ( GetMonitorInfoW ( MonitorFromWindow ( window, MONITOR_DEFAULTTONEAREST ), &monitor ) && EnumDisplaySettingsW ( monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode ) && mode.dmDisplayFrequency > 1 )
			return 1.0 / static_cast < double > ( mode.dmDisplayFrequency );

		return 1.0 / 60.0;

	}

	static void toast ( const wchar_t* text ) {

		frontend->toast ( text, now_seconds ( ) );
		render ( );

	}

	static void refresh_names ( ) {

		copy_wide ( disc_name, file->iso [ 0 ] && machine->disc.present ( ) ? leaf ( file->iso ) : L"", MAX_PATH );
		copy_wide ( disk_name, file->disk [ 0 ] ? leaf ( file->disk ) : L"", MAX_PATH );

	}

	static void release_keys ( ) {

		for ( int32_t extended = 0; extended < 2; extended++ ) {

			for ( int32_t scan = 0; scan < 128; scan++ ) {

				if ( !held [ extended ] [ scan ] )
					continue;

				held [ extended ] [ scan ] = false;

				if ( running )
					machine->key ( static_cast < uint8_t > ( scan | 0x80 ), extended != 0 );

			}

		}

		if ( buttons && running )
			machine->mouse ( 0, 0, 0, 0 );

		if ( pointer_buttons && running )
			machine->pointer ( pointer_x, pointer_y, 0, 0 );

		buttons = 0;
		pointer_buttons = 0;

	}

	static bool send_pointer ( LPARAM place, int32_t wheel, bool anywhere ) {

		float factor = scale ( );
		float x = static_cast < float > ( static_cast < int16_t > ( LOWORD ( place ) ) ) / factor;
		float y = static_cast < float > ( static_cast < int16_t > ( HIWORD ( place ) ) ) / factor;
		const render::rect& area = frontend->frame_area;

		if ( !running || !integrated || captured || area.w <= 0.0f || area.h <= 0.0f || ( !anywhere && !frontend->over_screen ( x, y ) ) )
			return false;

		pointer_x = static_cast < int32_t > ( ( x - area.x ) / area.w * static_cast < float > ( pc::usb_tablet::span ) );
		pointer_y = static_cast < int32_t > ( ( y - area.y ) / area.h * static_cast < float > ( pc::usb_tablet::span ) );
		machine->pointer ( pointer_x, pointer_y, wheel, pointer_buttons );

		return true;

	}

	static bool pointer_button ( LPARAM place, uint8_t bit, bool down ) {

		if ( down ) {

			uint8_t before = pointer_buttons;

			pointer_buttons |= bit;

			if ( !send_pointer ( place, 0, false ) ) {

				pointer_buttons = before;

				return false;

			}

			SetCapture ( window );

			return true;

		}

		if ( !( pointer_buttons & bit ) )
			return false;

		pointer_buttons &= static_cast < uint8_t > ( ~bit );
		send_pointer ( place, 0, true );

		if ( !pointer_buttons )
			ReleaseCapture ( );

		return true;

	}

	static void set_capture ( bool value );

	static void stop_typing ( );

	static void load_rules ( );

	static void save_preview ( ) {

		wchar_t folder [ MAX_PATH ];

		if ( smoke || !frame_width || !run->keep_preview || run->disposable || !vm::folder_of ( file_path, folder ) )
			return;

		int32_t width = frame_width;
		int32_t height = frame_height;
		int32_t step = 1;

		while ( width / step > 1024 || height / step > 512 )
			step++;

		width /= step;
		height /= step;

		uint32_t* shrunk = static_cast < uint32_t* > ( runtime_allocate ( sizeof ( uint32_t ) * static_cast < size_t > ( width ) * height ) );

		for ( int32_t y = 0; y < height; y++ ) {

			for ( int32_t x = 0; x < width; x++ )
				shrunk [ y * width + x ] = frame [ ( y * step ) * frame_width + x * step ];

		}

		vm::write_preview ( folder, shrunk, width, height );
		runtime_release ( shrunk );

	}

	static void power_on ( ) {

		pc::computer_options options;

		options.processors = file->processors;
		options.memory = static_cast < uint64_t > ( file->memory_mb ) << 20;
		options.cap = file->cap;
		options.optical_first = !file->disk_first;
		options.uefi = file->uefi;
		options.network = file->network;
		options.sharing = ( file->clipboard & 1 ? pc::SHARE_TEXT_IN : 0u ) | ( file->clipboard & 2 ? pc::SHARE_TEXT_OUT : 0u ) | ( file->file_sharing & 1 ? pc::SHARE_FILES_IN : 0u ) | ( file->file_sharing & 2 ? pc::SHARE_FILES_OUT : 0u );
		options.screen_width = run->screen_width;
		options.screen_height = run->screen_height;
		options.local_network = run->local_network;
		options.ask_internet = run->ask_internet;
		options.fresh_identity = run->fresh_identity;
		options.disposable = run->disposable;

		for ( int32_t i = 0; run->use_proxy && i < 127 && run->proxy [ i ]; i++ ) {

			options.proxy [ i ] = run->proxy [ i ];
			options.proxy [ i + 1 ] = 0;

		}
		machine->tools.set_inbox ( clipboard_inbox );
		tools_linked = false;
		asks.clear ( );
		ask_remember = false;

		if ( run->ask_internet )
			load_rules ( );
		copy_wide ( options.iso, file->iso, MAX_PATH );
		copy_wide ( options.disk, file->disk, MAX_PATH );

		wchar_t folder [ MAX_PATH ];
		char error [ 160 ] = { };
		bool helped = true;

		if ( !smoke && file->uefi && vm::folder_of ( file_path, folder ) ) {

			base::writer variables;

			variables.add ( folder ).add ( L"\\nvram.bin" );
			copy_wide ( options.variables, variables.text ( ), MAX_PATH );

		}

		base::writer answers;

		if ( file->unattended && vm::folder_of ( file_path, folder ) )
			answers.add ( folder ).add ( L"\\setup-answers.iso" );

		if ( answers.size ( ) && GetFileAttributesW ( answers.text ( ) ) != INVALID_FILE_ATTRIBUTES )
			copy_wide ( options.helper, answers.text ( ), MAX_PATH );
		else if ( base::same_text ( file->os, "windows11" ) && vm::folder_of ( file_path, folder ) )
			helped = pc::make_setup_helper ( folder, options.helper, error, sizeof ( error ) );

		failed = false;
		problem [ 0 ] = 0;
		frame_width = 0;
		frame_height = 0;

		for ( auto& row : held ) {

			for ( bool& key : row )
				key = false;

		}

		running = machine->power_on ( options );

		if ( !running ) {

			failed = true;
			MultiByteToWideChar ( CP_UTF8, 0, machine->problem, -1, problem, 255 );

		} else if ( file->iso [ 0 ] && !machine->disc.present ( ) )
			toast ( L"The disc image couldn't be opened, so the drive starts empty" );
		else if ( !helped )
			toast ( L"Couldn't make the Windows 11 setup helper disc, so Setup may ask for TPM 2.0" );

		refresh_names ( );
		next_frame = now_seconds ( );
		fps_since = next_frame;
		fps_frames = 0;
		fps_shown = -1;
		render ( );

	}

	static void power_off ( bool keep_screen ) {

		set_capture ( false );

		if ( keep_screen )
			save_preview ( );

		stop_typing ( );
		machine->power_off ( );
		running = false;
		render ( );

	}

	static void restart ( ) {

		set_capture ( false );
		release_keys ( );
		stop_typing ( );
		asks.clear ( );

		if ( !machine->restart ( ) ) {

			running = false;
			failed = true;
			MultiByteToWideChar ( CP_UTF8, 0, machine->problem, -1, problem, 255 );

		}

		render ( );

	}

	static void send_key ( UINT key, uint32_t scan, bool extended, bool down ) {

		if ( !running )
			return;

		if ( key == VK_PAUSE || key == VK_CANCEL )
			return;

		if ( key == VK_NUMLOCK ) {

			scan = 0x45;
			extended = false;

		} else if ( key == VK_SNAPSHOT ) {

			scan = 0x37;
			extended = true;

		} else if ( key == VK_RSHIFT )
			extended = false;

		if ( !scan || scan > 0x7F )
			return;

		if ( !down && !held [ extended ] [ scan ] )
			return;

		held [ extended ] [ scan ] = down;
		machine->key ( static_cast < uint8_t > ( down ? scan : scan | 0x80 ), extended );

	}

	static void control_alt_delete ( ) {

		if ( !running )
			return;

		machine->key ( 0x1D, false );
		machine->key ( 0x38, false );
		machine->key ( 0x53, true );
		Sleep ( 30 );
		machine->key ( 0xD3, true );
		machine->key ( 0xB8, false );
		machine->key ( 0x9D, false );

	}

	static void toggle_fullscreen ( ) {

		LONG_PTR style = GetWindowLongPtrW ( window, GWL_STYLE );

		if ( !fullscreen ) {

			MONITORINFO monitor = { sizeof ( MONITORINFO ) };

			GetWindowPlacement ( window, &saved_place );
			GetMonitorInfoW ( MonitorFromWindow ( window, MONITOR_DEFAULTTONEAREST ), &monitor );
			SetWindowLongPtrW ( window, GWL_STYLE, ( style & ~WS_OVERLAPPEDWINDOW ) | WS_POPUP );
			SetWindowPos ( window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top, monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER );
			fullscreen = true;
			toast ( L"Right Ctrl+F leaves fullscreen" );

		} else {

			SetWindowLongPtrW ( window, GWL_STYLE, ( style & ~WS_POPUP ) | WS_OVERLAPPEDWINDOW );
			SetWindowPlacement ( window, &saved_place );
			SetWindowPos ( window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER );
			fullscreen = false;
			render ( );

		}

	}

	static void take_screenshot ( ) {

		wchar_t path [ MAX_PATH ];

		if ( !running || !frame_width ) {

			toast ( L"Start the machine first to take a screenshot" );

			return;

		}

		if ( screenshot_path ( path ) && png::save ( path, frame, frame_width, frame_height ) )
			toast ( L"Screenshot saved to Pictures\\Coldboot" );
		else
			toast ( L"Couldn't save the screenshot" );

	}

	static bool host_key ( WPARAM key, bool extended, bool down ) {

		if ( key == VK_CONTROL && extended ) {

			if ( down ) {

				if ( !host_down ) {

					host_down = true;
					host_used = false;

				}

			} else {

				if ( host_down && !host_used && captured )
					set_capture ( false );

				host_down = false;

			}

			return true;

		}

		if ( !host_down )
			return false;

		if ( down ) {

			host_used = true;

			if ( key == 'F' )
				PostMessageW ( window, WM_APP + 1, 0, 0 );
			else if ( key == VK_DELETE )
				PostMessageW ( window, WM_APP + 2, 0, 0 );
			else if ( key == 'S' )
				PostMessageW ( window, WM_APP + 3, 0, 0 );

		}

		return true;

	}

	static LRESULT __stdcall keyboard_filter ( int code, WPARAM message, LPARAM data ) {

		if ( code == HC_ACTION && captured && GetForegroundWindow ( ) == window ) {

			const KBDLLHOOKSTRUCT* info = reinterpret_cast < const KBDLLHOOKSTRUCT* > ( data );
			bool down = !( info->flags & LLKHF_UP );
			bool extended = ( info->flags & LLKHF_EXTENDED ) != 0;
			WPARAM key = info->vkCode;

			if ( key == VK_LCONTROL || key == VK_RCONTROL )
				key = VK_CONTROL;

			if ( !host_key ( key, extended, down ) )
				send_key ( static_cast < UINT > ( info->vkCode ), info->scanCode & 0xFF, extended, down );

			return 1;

		}

		return CallNextHookEx ( keyboard_hook, code, message, data );

	}

	static void set_capture ( bool value ) {

		value = value && running;

		if ( value == captured )
			return;

		captured = value;

		RAWINPUTDEVICE device = { 0x01, 0x02, 0, nullptr };

		if ( captured ) {

			RECT area;
			POINT corner = { 0, 0 };
			float factor = scale ( );

			area.left = static_cast < LONG > ( frontend->frame_area.x * factor );
			area.top = static_cast < LONG > ( frontend->frame_area.y * factor );
			area.right = static_cast < LONG > ( frontend->frame_area.right ( ) * factor );
			area.bottom = static_cast < LONG > ( frontend->frame_area.bottom ( ) * factor );
			ClientToScreen ( window, &corner );
			OffsetRect ( &area, corner.x, corner.y );
			ClipCursor ( &area );
			ShowCursor ( FALSE );
			device.dwFlags = 0;
			device.hwndTarget = window;
			RegisterRawInputDevices ( &device, 1, sizeof ( device ) );

			if ( !keyboard_hook )
				keyboard_hook = SetWindowsHookExW ( WH_KEYBOARD_LL, &keyboard_filter, GetModuleHandleW ( nullptr ), 0 );

		} else {

			ClipCursor ( nullptr );
			ShowCursor ( TRUE );
			device.dwFlags = RIDEV_REMOVE;
			RegisterRawInputDevices ( &device, 1, sizeof ( device ) );

			if ( keyboard_hook ) {

				UnhookWindowsHookEx ( keyboard_hook );
				keyboard_hook = nullptr;

			}

			release_keys ( );

		}

		render ( );

	}

	static void raw_mouse ( LPARAM handle ) {

		RAWINPUT input;
		UINT size = sizeof ( input );

		if ( GetRawInputData ( reinterpret_cast < HRAWINPUT > ( handle ), RID_INPUT, &input, &size, sizeof ( RAWINPUTHEADER ) ) == static_cast < UINT > ( -1 ) || input.header.dwType != RIM_TYPEMOUSE )
			return;

		const RAWMOUSE& mouse = input.data.mouse;
		USHORT flags = mouse.usButtonFlags;
		int32_t wheel = 0;

		if ( flags & RI_MOUSE_LEFT_BUTTON_DOWN )
			buttons |= 1;

		if ( flags & RI_MOUSE_LEFT_BUTTON_UP )
			buttons &= static_cast < uint8_t > ( ~1 );

		if ( flags & RI_MOUSE_RIGHT_BUTTON_DOWN )
			buttons |= 2;

		if ( flags & RI_MOUSE_RIGHT_BUTTON_UP )
			buttons &= static_cast < uint8_t > ( ~2 );

		if ( flags & RI_MOUSE_MIDDLE_BUTTON_DOWN )
			buttons |= 4;

		if ( flags & RI_MOUSE_MIDDLE_BUTTON_UP )
			buttons &= static_cast < uint8_t > ( ~4 );

		if ( flags & RI_MOUSE_WHEEL )
			wheel = -static_cast < SHORT > ( mouse.usButtonData ) / WHEEL_DELTA;

		int32_t dx = 0;
		int32_t dy = 0;

		if ( !( mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) ) {

			dx = mouse.lLastX;
			dy = mouse.lLastY;

		}

		if ( running )
			machine->mouse ( dx, dy, wheel, buttons );

	}

	static void stop_typing ( ) {

		if ( typist ) {

			InterlockedExchange ( &typing_quit, 1 );
			WaitForSingleObject ( typist, INFINITE );
			CloseHandle ( typist );
			typist = nullptr;

		}

		runtime_release ( typed );
		typed = nullptr;

	}

	static bool key_for ( wchar_t c, uint8_t& scan, bool& shifted ) {

		static const char letters [ 26 ] = { 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C };
		static const char plain [ ] = "1234567890-=[];'`\\,./ \n\t";
		static const uint8_t plain_scans [ ] = { 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x1A, 0x1B, 0x27, 0x28, 0x29, 0x2B, 0x33, 0x34, 0x35, 0x39, 0x1C, 0x0F };
		static const char shifted_keys [ ] = "!@#$%^&*()_+{}:\"~|<>?";
		static const uint8_t shifted_scans [ ] = { 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x1A, 0x1B, 0x27, 0x28, 0x29, 0x2B, 0x33, 0x34, 0x35 };

		shifted = false;

		if ( c >= L'a' && c <= L'z' ) {

			scan = static_cast < uint8_t > ( letters [ c - L'a' ] );

			return true;

		}

		if ( c >= L'A' && c <= L'Z' ) {

			scan = static_cast < uint8_t > ( letters [ c - L'A' ] );
			shifted = true;

			return true;

		}

		for ( int32_t i = 0; plain [ i ]; i++ ) {

			if ( c == static_cast < wchar_t > ( plain [ i ] ) ) {

				scan = plain_scans [ i ];

				return true;

			}

		}

		for ( int32_t i = 0; shifted_keys [ i ]; i++ ) {

			if ( c == static_cast < wchar_t > ( shifted_keys [ i ] ) ) {

				scan = shifted_scans [ i ];
				shifted = true;

				return true;

			}

		}

		return false;

	}

	static DWORD __stdcall type_loop ( void* ) {

		for ( int32_t i = 0; typed [ i ] && !typing_quit; i++ ) {

			uint8_t scan = 0;
			bool shifted = false;

			if ( typed [ i ] == L'\r' || !key_for ( typed [ i ], scan, shifted ) )
				continue;

			if ( shifted )
				machine->key ( 0x2A, false );

			machine->key ( scan, false );
			Sleep ( 6 );
			machine->key ( static_cast < uint8_t > ( scan | 0x80 ), false );

			if ( shifted )
				machine->key ( 0xAA, false );

			Sleep ( 8 );

		}

		return 0;

	}

	static void type_clipboard ( ) {

		if ( !running )
			return;

		stop_typing ( );

		if ( !OpenClipboard ( window ) ) {

			toast ( L"Couldn't open the clipboard" );

			return;

		}

		HANDLE data = GetClipboardData ( CF_UNICODETEXT );
		const wchar_t* text = data ? static_cast < const wchar_t* > ( GlobalLock ( data ) ) : nullptr;

		if ( text ) {

			int32_t length = 0;

			while ( text [ length ] && length < 65536 )
				length++;

			typed = static_cast < wchar_t* > ( runtime_allocate ( sizeof ( wchar_t ) * ( length + 1 ) ) );

			for ( int32_t i = 0; i < length; i++ )
				typed [ i ] = text [ i ];

			typed [ length ] = 0;
			GlobalUnlock ( data );

		}

		CloseClipboard ( );

		if ( !typed || !typed [ 0 ] ) {

			toast ( L"The clipboard has no text to type" );

			return;

		}

		typing_quit = 0;
		typist = CreateThread ( nullptr, 0, &type_loop, nullptr, 0, nullptr );
		toast ( L"Typing the clipboard into the machine" );

	}

	static void add_tree ( pc::iso_builder& builder, const wchar_t* source, const char* name, int32_t& added ) {

		DWORD attributes = GetFileAttributesW ( source );

		if ( attributes == INVALID_FILE_ATTRIBUTES || added > 20000 )
			return;

		if ( !( attributes & FILE_ATTRIBUTE_DIRECTORY ) ) {

			if ( builder.add_file ( name, source ) )
				added++;

			return;

		}

		base::writer pattern;
		WIN32_FIND_DATAW found;

		pattern.add ( source ).add ( L"\\*" );

		HANDLE search = FindFirstFileW ( pattern.text ( ), &found );

		if ( search == INVALID_HANDLE_VALUE )
			return;

		do {

			if ( found.cFileName [ 0 ] == L'.' && ( !found.cFileName [ 1 ] || ( found.cFileName [ 1 ] == L'.' && !found.cFileName [ 2 ] ) ) )
				continue;

			base::writer child;
			char leaf [ 192 ];
			char path [ 520 ];
			int32_t used = 0;

			child.add ( source ).add ( L"\\" ).add ( found.cFileName );

			int32_t length = WideCharToMultiByte ( CP_UTF8, 0, found.cFileName, -1, leaf, sizeof ( leaf ), nullptr, nullptr );

			if ( length <= 1 )
				continue;

			for ( int32_t i = 0; name [ i ] && used < 300; i++ )
				path [ used++ ] = name [ i ];

			path [ used++ ] = '/';

			for ( int32_t i = 0; leaf [ i ] && used < 518; i++ )
				path [ used++ ] = leaf [ i ];

			path [ used ] = 0;
			add_tree ( builder, child.text ( ), path, added );

		} while ( FindNextFileW ( search, &found ) );

		FindClose ( search );

	}

	static DWORD __stdcall build_sent ( void* context ) {

		send_job* job = static_cast < send_job* > ( context );
		pc::iso_builder* builder = new pc::iso_builder ( );
		char names [ sent_room ] [ 192 ] = { };
		char error [ 160 ] = { };

		builder->set_label ( "COLDBOOT_FILES" );

		for ( int32_t i = 0; i < job->count; i++ ) {

			const wchar_t* leaf = job->paths [ i ];

			for ( const wchar_t* at = job->paths [ i ]; *at; at++ ) {

				if ( *at == L'\\' || *at == L'/' )
					leaf = at + 1;

			}

			char name [ 192 ];

			if ( WideCharToMultiByte ( CP_UTF8, 0, leaf, -1, name, 180, nullptr, nullptr ) <= 1 )
				continue;

			for ( int32_t copy = 2; copy < 100; copy++ ) {

				bool taken = false;

				for ( int32_t j = 0; j < i; j++ )
					taken = taken || base::same_text ( names [ j ], name );

				if ( !taken )
					break;

				int32_t length = base::text_length ( name );

				name [ length ] = ' ';
				name [ length + 1 ] = static_cast < char > ( '0' + copy % 10 );
				name [ length + 2 ] = 0;

			}

			for ( int32_t c = 0; c < 191 && name [ c ]; c++ ) {

				names [ i ] [ c ] = name [ c ];
				names [ i ] [ c + 1 ] = 0;

			}

			add_tree ( *builder, job->paths [ i ], name, job->files );

		}

		job->ok = job->files > 0 && builder->write ( job->target, error, sizeof ( error ) );
		delete builder;
		PostMessageW ( window, WM_APP + 4, 0, reinterpret_cast < LPARAM > ( job ) );

		return 0;

	}

	static void send_files ( const wchar_t ( *paths ) [ MAX_PATH ], int32_t count ) {

		if ( !running || !count )
			return;

		if ( machine->tools.connected ( ) && ( machine->tools.current_policy ( ) & pc::SHARE_FILES_IN ) ) {

			if ( machine->tools.send_files ( paths, count, pc::PURPOSE_SENT, ~0ull ) )
				toast ( L"Sending the files to the machine's desktop\x2026" );
			else
				toast ( L"Still sending the last files. Try again in a moment." );

			return;

		}

		if ( sender ) {

			toast ( L"Still copying the last files. Try again in a moment." );

			return;

		}

		if ( !sent )
			sent = static_cast < wchar_t ( * ) [ MAX_PATH ] > ( runtime_allocate_zero ( sizeof ( wchar_t ) * MAX_PATH * sent_room ) );

		for ( int32_t i = 0; i < count && sent_count < sent_room; i++ ) {

			bool known = false;

			for ( int32_t j = 0; j < sent_count; j++ )
				known = known || base::same_text ( sent [ j ], paths [ i ] );

			if ( !known )
				copy_wide ( sent [ sent_count++ ], paths [ i ], MAX_PATH );

		}

		send_job* job = new send_job ( );
		wchar_t folder [ MAX_PATH ];
		base::writer target;

		if ( smoke || !vm::folder_of ( file_path, folder ) ) {

			GetTempPathW ( MAX_PATH, folder );

			int32_t length = 0;

			while ( folder [ length ] )
				length++;

			if ( length && folder [ length - 1 ] == L'\\' )
				folder [ length - 1 ] = 0;

		}

		sent_flip = 1 - sent_flip;
		target.add ( folder ).add ( sent_flip ? L"\\sent-files-1.iso" : L"\\sent-files-2.iso" );
		copy_wide ( job->target, target.text ( ), MAX_PATH );
		job->count = sent_count;

		for ( int32_t i = 0; i < sent_count; i++ )
			copy_wide ( job->paths [ i ], sent [ i ], MAX_PATH );

		sender = CreateThread ( nullptr, 0, &build_sent, job, 0, nullptr );

		if ( !sender ) {

			delete job;
			toast ( L"Couldn't start copying the files" );

			return;

		}

		toast ( L"Copying the files onto a disc for the machine\x2026" );

	}

	static void sent_ready ( send_job* job ) {

		if ( sender ) {

			WaitForSingleObject ( sender, INFINITE );
			CloseHandle ( sender );
			sender = nullptr;

		}

		if ( !job->ok ) {

			toast ( L"Couldn't put those files on a disc" );
			DeleteFileW ( job->target );
			delete job;

			return;

		}

		if ( running && machine->change_second_disc ( job->target ) ) {

			if ( sent_disc [ 0 ] && !base::same_text ( sent_disc, job->target ) )
				DeleteFileW ( sent_disc );

			copy_wide ( sent_disc, job->target, MAX_PATH );

			base::writer note;

			note.add ( L"Sent " ).number ( job->files ).add ( job->files == 1 ? L" file" : L" files" ).add ( L". They're on the machine's second DVD drive." );
			toast ( note.text ( ) );

		} else {

			DeleteFileW ( job->target );
			toast ( L"The machine isn't running anymore" );

		}

		delete job;

	}

	static void pick_and_send ( ) {

		set_capture ( false );

		wchar_t ( *picked ) [ MAX_PATH ] = static_cast < wchar_t ( * ) [ MAX_PATH ] > ( runtime_allocate_zero ( sizeof ( wchar_t ) * MAX_PATH * sent_room ) );
		int32_t count = open_files ( window, picked, sent_room );

		send_files ( picked, count );
		runtime_release ( picked );

	}

	static void install_tools_disc ( ) {

		if ( !running )
			return;

		wchar_t self [ MAX_PATH ] = { };
		wchar_t folder [ MAX_PATH ] = { };
		wchar_t note_path [ MAX_PATH ] = { };
		base::writer target;
		char error [ 160 ] = { };

		GetModuleFileNameW ( nullptr, self, MAX_PATH );

		if ( smoke || !vm::folder_of ( file_path, folder ) ) {

			GetTempPathW ( MAX_PATH, folder );

			int32_t length = base::text_length ( folder );

			if ( length && folder [ length - 1 ] == L'\\' )
				folder [ length - 1 ] = 0;

		}

		target.add ( folder ).add ( L"\\coldboot-tools.iso" );

		base::writer note;

		note.add ( folder ).add ( L"\\coldboot-tools-readme.txt" );
		copy_wide ( note_path, note.text ( ), MAX_PATH );

		static const char readme [ ] =
			"Coldboot Tools\r\n"
			"\r\n"
			"Run \"Coldboot Tools.exe\" inside the machine to install them for this user.\r\n"
			"They start with Windows from then on and share the clipboard and files with your PC,\r\n"
			"in whichever directions the machine's settings allow.\r\n";

		pc::iso_builder* builder = new pc::iso_builder ( );

		builder->set_label ( "COLDBOOT_TOOLS" );

		bool ok = files::write ( note_path, readme, static_cast < int32_t > ( sizeof ( readme ) - 1 ) ) && builder->add_file ( "Coldboot Tools.exe", self ) && builder->add_file ( "Read me.txt", note_path );

		if ( ok && sent_disc [ 0 ] && base::same_text ( sent_disc, target.text ( ) ) )
			machine->change_second_disc ( nullptr );

		ok = ok && builder->write ( target.text ( ), error, sizeof ( error ) );
		delete builder;
		DeleteFileW ( note_path );

		if ( ok && machine->change_second_disc ( target.text ( ) ) ) {

			if ( sent_disc [ 0 ] && !base::same_text ( sent_disc, target.text ( ) ) )
				DeleteFileW ( sent_disc );

			copy_wide ( sent_disc, target.text ( ), MAX_PATH );
			toast ( L"Tools disc inserted. In the machine, open it and run Coldboot Tools.exe." );

		} else
			toast ( L"Couldn't make the tools disc" );

	}

	static void share_host_clipboard ( ) {

		DWORD sequence = GetClipboardSequenceNumber ( );

		if ( !running || sequence == ignore_sequence || !machine->tools.connected ( ) )
			return;

		uint32_t policy = machine->tools.current_policy ( );

		if ( clipboard_has_files ( ) ) {

			if ( !( policy & pc::SHARE_FILES_IN ) )
				return;

			wchar_t ( *paths ) [ MAX_PATH ] = static_cast < wchar_t ( * ) [ MAX_PATH ] > ( runtime_allocate_zero ( sizeof ( wchar_t ) * MAX_PATH * sent_room ) );
			int32_t count = read_clipboard_files ( window, paths, sent_room );

			if ( count && !machine->tools.send_files ( paths, count, pc::PURPOSE_CLIPBOARD, pc::clipboard_file_limit ) )
				toast ( machine->tools.busy ( ) ? L"Still sending files to the machine, so this copy wasn't shared" : L"Those files are over 1 GB, so they weren't put on the machine's clipboard. Use Send files." );

			runtime_release ( paths );

			return;

		}

		if ( clipboard_has_text ( ) && ( policy & pc::SHARE_TEXT_IN ) ) {

			base::list < wchar_t > text;

			if ( read_clipboard_text ( window, text ) )
				machine->tools.send_text ( text.data ( ), static_cast < uint32_t > ( text.size ( ) ) );

		}

	}

	static bool rules_path ( base::writer& out ) {

		wchar_t folder [ MAX_PATH ];

		if ( smoke || !vm::folder_of ( file_path, folder ) )
			return false;

		out.add ( folder ).add ( L"\\network-rules.txt" );

		return true;

	}

	static void load_rules ( ) {

		base::writer path;
		base::list < char > text;

		rules.clear ( );

		if ( !rules_path ( path ) || !files::read ( path.text ( ), text ) )
			return;

		for ( int32_t start = 0; start < text.count ( ); ) {

			int32_t end = start;

			while ( end < text.count ( ) && text [ end ] != '\n' && text [ end ] != 0 )
				end++;

			int32_t length = end - start;

			while ( length && text [ start + length - 1 ] == '\r' )
				length--;

			bool allow = length > 6 && text [ start ] == 'a' && text [ start + 5 ] == ' ';
			bool block = length > 6 && text [ start ] == 'b' && text [ start + 5 ] == ' ';

			if ( allow || block ) {

				app_rule rule;
				int32_t written = MultiByteToWideChar ( CP_UTF8, 0, text.data ( ) + start + 6, length - 6, rule.app, 63 );

				rule.app [ written > 0 ? written : 0 ] = 0;
				rule.allow = allow;
				rule.saved = true;
				rules.push ( rule );

			}

			start = end + 1;

		}

	}

	static void save_rules ( ) {

		base::writer path;
		base::list < char > text;

		if ( !rules_path ( path ) )
			return;

		for ( const app_rule& rule : rules ) {

			char bytes [ 256 ];
			int32_t length = WideCharToMultiByte ( CP_UTF8, 0, rule.app, -1, bytes, sizeof ( bytes ), nullptr, nullptr );

			if ( !rule.saved || length <= 1 )
				continue;

			for ( const char* word = rule.allow ? "allow " : "block "; *word; word++ )
				text.push ( *word );

			for ( int32_t i = 0; i < length - 1; i++ )
				text.push ( bytes [ i ] );

			text.push ( '\n' );

		}

		files::write ( path.text ( ), text.data ( ), text.count ( ) );

	}

	static int32_t rule_for ( const wchar_t* app ) {

		for ( const app_rule& rule : rules ) {

			if ( CompareStringOrdinal ( rule.app, -1, app, -1, TRUE ) == CSTR_EQUAL )
				return rule.allow ? 1 : 0;

		}

		return -1;

	}

	static void pump_asks ( double now ) {

		if ( !running || !machine->router || !run->ask_internet )
			return;

		net::nat::access_request request;
		bool changed = false;

		while ( machine->router->next_request ( request ) ) {

			ask_item item;

			item.id = request.id;
			item.protocol = request.protocol;
			item.guest_port = request.guest_port;
			item.remote = request.remote;
			item.remote_port = request.remote_port;
			item.since = now;
			MultiByteToWideChar ( CP_UTF8, 0, request.host, -1, item.host, 255 );
			item.named = !machine->tools.ask_owner ( request.id, request.protocol, request.guest_port );
			asks.push ( item );
			changed = true;

		}

		for ( int32_t i = 0; i < asks.count ( ); ) {

			ask_item& item = asks [ i ];

			if ( !item.named && now - item.since > 1.0 ) {

				item.named = true;
				changed = true;

			}

			int32_t rule = item.named ? rule_for ( item.app ) : -1;

			if ( rule >= 0 || now - item.since > 110.0 ) {

				machine->router->decide ( item.id, rule == 1 );
				asks.remove_at ( static_cast < size_t > ( i ) );
				changed = true;

				continue;

			}

			i++;

		}

		if ( changed ) {

			int32_t others = 0;

			input_seen = true;

			if ( waiting_ask ( others ) && captured )
				set_capture ( false );

		}

	}

	static void answer_ask ( bool allow ) {

		int32_t others = 0;
		const ask_item* first = waiting_ask ( others );

		if ( !first || !machine->router )
			return;

		app_rule rule;

		copy_wide ( rule.app, first->app, 64 );
		rule.allow = allow;
		rule.saved = ask_remember;
		rules.push ( rule );

		if ( ask_remember )
			save_rules ( );

		ask_remember = false;

		for ( int32_t i = 0; i < asks.count ( ); ) {

			if ( asks [ i ].named && CompareStringOrdinal ( asks [ i ].app, -1, rule.app, -1, TRUE ) == CSTR_EQUAL ) {

				machine->router->decide ( asks [ i ].id, allow );
				asks.remove_at ( static_cast < size_t > ( i ) );

				continue;

			}

			i++;

		}

	}

	static void drain_tools ( ) {

		pc::tools_event item;

		while ( running && machine->tools.next_event ( item ) ) {

			switch ( item.kind ) {

				case pc::TOOLS_OWNER_FOUND:

					for ( ask_item& waiting : asks ) {

						if ( waiting.id == item.count && !waiting.named ) {

							copy_wide ( waiting.app, item.text.empty ( ) ? L"" : item.text.data ( ), 64 );
							waiting.named = true;
							input_seen = true;

						}

					}

					break;

				case pc::TOOLS_CONNECTED:
					tools_linked = true;
					toast ( L"Coldboot Tools connected. Copy and paste work with the machine now." );
					break;

				case pc::TOOLS_DISCONNECTED:
					tools_linked = false;
					break;

				case pc::TOOLS_GOT_TEXT:

					if ( !item.text.empty ( ) )
						ignore_sequence = write_clipboard_text ( window, item.text.data ( ) );

					break;

				case pc::TOOLS_GOT_FILES: {

					item.items.push ( 0 );
					ignore_sequence = write_clipboard_files ( window, item.items.data ( ) );

					base::writer note;

					note.add ( L"Copied " ).number ( item.count ).add ( item.count == 1 ? L" item" : L" items" ).add ( L" from the machine. Paste them anywhere on your PC." );
					toast ( note.text ( ) );
					break;

				}

				case pc::TOOLS_SENT:

					if ( item.purpose == pc::PURPOSE_SENT )
						toast ( L"Sent. The files are in \"From your PC\" on the machine's desktop." );

					break;

				case pc::TOOLS_REFUSED:
					toast ( L"The machine tried to share files with your PC, but this machine's settings don't allow it" );
					break;

				default:
					break;

			}

		}

	}

	static void insert_path ( const wchar_t* path ) {

		copy_wide ( file->iso, path, MAX_PATH );

		if ( running && !machine->change_disc ( path ) ) {

			toast ( L"That file couldn't be opened as a disc image" );
			file->iso [ 0 ] = 0;

		} else {

			base::writer text;

			text.add ( L"Inserted " ).add ( leaf ( path ) );
			toast ( text.text ( ) );

		}

		save_machine ( );
		refresh_names ( );
		render ( );

	}

	static void insert_disc ( ) {

		wchar_t path [ MAX_PATH ];

		set_capture ( false );

		if ( open_file ( window, FILE_IMAGE, path ) )
			insert_path ( path );

	}

	static void dropped ( HDROP drop ) {

		wchar_t ( *paths ) [ MAX_PATH ] = static_cast < wchar_t ( * ) [ MAX_PATH ] > ( runtime_allocate_zero ( sizeof ( wchar_t ) * MAX_PATH * sent_room ) );
		UINT total = DragQueryFileW ( drop, 0xFFFFFFFF, nullptr, 0 );
		int32_t count = 0;

		for ( UINT i = 0; i < total && count < sent_room; i++ ) {

			if ( DragQueryFileW ( drop, i, paths [ count ], MAX_PATH ) )
				count++;

		}

		DragFinish ( drop );

		int32_t length = count == 1 ? base::text_length ( paths [ 0 ] ) : 0;
		bool image = length > 4 && paths [ 0 ] [ length - 4 ] == L'.';

		for ( int32_t i = 0; image && i < 3; i++ )
			image = ( paths [ 0 ] [ length - 3 + i ] | 0x20 ) == L"iso" [ i ];

		if ( image && running )
			insert_path ( paths [ 0 ] );
		else if ( running )
			send_files ( paths, count );
		else
			toast ( L"Start the machine first, then drop files on it" );

		runtime_release ( paths );

	}

	static void eject_disc ( ) {

		if ( running )
			machine->change_disc ( nullptr );

		file->iso [ 0 ] = 0;
		save_machine ( );
		refresh_names ( );
		toast ( L"Disc ejected" );

	}

	static void act ( ui::console_action action ) {

		switch ( action ) {

			case ui::TOOL_START:
				power_on ( );
				return;

			case ui::TOOL_SHUT_DOWN:

				if ( running ) {

					machine->press_power ( );
					toast ( L"Pressed the power button. Use Turn off if the guest ignores it." );

				}

				return;

			case ui::TOOL_TURN_OFF:
				power_off ( true );
				return;

			case ui::TOOL_RESET:
				restart ( );
				toast ( L"Reset" );
				return;

			case ui::TOOL_KEYS:
				control_alt_delete ( );
				return;

			case ui::TOOL_DISC:
				insert_disc ( );
				return;

			case ui::TOOL_EJECT:
				eject_disc ( );
				return;

			case ui::TOOL_SHOT:
				take_screenshot ( );
				return;

			case ui::TOOL_FILES:
				pick_and_send ( );
				return;

			case ui::TOOL_TYPE:
				type_clipboard ( );
				return;

			case ui::TOOL_TOOLS:
				install_tools_disc ( );
				return;

			case ui::TOOL_ALLOW:
				answer_ask ( true );
				render ( );
				return;

			case ui::TOOL_BLOCK:
				answer_ask ( false );
				render ( );
				return;

			case ui::TOOL_REMEMBER:
				ask_remember = !ask_remember;
				render ( );
				return;

			case ui::TOOL_FULLSCREEN:
				toggle_fullscreen ( );
				return;

			case ui::TOOL_SCREEN:

				if ( running && !captured ) {

					set_capture ( true );
					toast ( L"Mouse and keyboard captured. Right Ctrl releases them." );

				}

				return;

			default:
				return;

		}

	}

	static bool tick ( ) {

		if ( !running )
			return false;

		drain_tools ( );
		pump_asks ( now_seconds ( ) );

		bool ready = machine->pointer_ready ( );

		if ( ready != integrated ) {

			integrated = ready;

			if ( integrated && captured )
				set_capture ( false );

			if ( integrated && !integration_told ) {

				integration_told = true;
				toast ( L"Mouse integration is on, so the pointer moves freely in and out" );

			}

		}

		switch ( machine->poll ( ) ) {

			case pc::EVENT_RESET:
				restart ( );
				return true;

			case pc::EVENT_OFF:
				power_off ( true );
				toast ( L"The machine shut down" );
				return true;

			case pc::EVENT_FAILED:
				set_capture ( false );
				save_preview ( );
				MultiByteToWideChar ( CP_UTF8, 0, machine->problem, -1, problem, 255 );
				stop_typing ( );
				machine->power_off ( );
				running = false;
				failed = true;
				render ( );
				return true;

			default:
				break;

		}

		double now = now_seconds ( );

		capture ( now );

		if ( !frame_wanted ( now ) )
			return false;

		paint ( now );

		return true;

	}

	static void smoke_tick ( ) {

		double elapsed = now_seconds ( ) - smoke_started;

		if ( record_seconds > 0.0 ) {

			int32_t others = 0;
			double now = now_seconds ( );

			if ( !recorder && elapsed < record_from )
				return;

			int32_t length = base::text_length ( record_path );
			bool still = length > 4 && ( record_path [ length - 3 ] | 0x20 ) == L'p' && ( record_path [ length - 2 ] | 0x20 ) == L'n' && ( record_path [ length - 1 ] | 0x20 ) == L'g';

			if ( still ) {

				if ( shot_taken )
					DestroyWindow ( window );
				else if ( !shot_due ) {

					shot_due = true;
					input_seen = true;

				}

				return;

			}

			if ( !recorder ) {

				RECT client;

				GetClientRect ( window, &client );
				recorder = new gif::writer ( );

				int32_t across = 800;
				int32_t down = client.right > 0 ? static_cast < int32_t > ( static_cast < int64_t > ( across ) * client.bottom / client.right ) : 600;

				if ( !recorder->open ( record_path, across, down ) ) {

					delete recorder;
					recorder = nullptr;
					record_seconds = 0.0;

				}

				record_started = now;
				record_next = now;

			}

			if ( waiting_ask ( others ) ) {

				if ( ask_seen == 0.0 )
					ask_seen = now;
				else if ( now - ask_seen > 2.5 ) {

					answer_ask ( true );
					ask_seen = 0.0;
					input_seen = true;

				}

			} else
				ask_seen = 0.0;

			if ( recorder && now - record_started >= record_seconds ) {

				recorder->close ( );
				delete recorder;
				recorder = nullptr;
				record_seconds = 0.0;
				DestroyWindow ( window );

			}

			return;

		}

		if ( smoke_step == 0 && elapsed >= 8.0 ) {

			base::writer path;
			base::writer report;
			char bytes [ 512 ];

			path.add ( smoke_folder ).add ( L"\\pc_console.png" );
			render ( );
			display->save ( path.text ( ) );
			report.add ( L"running " ).add ( running ? L"yes" : L"no" ).add ( L"\r\nscreen " ).number ( frame_width ).add ( L"x" ).number ( frame_height ).add ( L"\r\ndisc " ).add ( disc_name [ 0 ] ? disc_name : L"-" ).add ( L"\r\n" );

			int32_t length = WideCharToMultiByte ( CP_UTF8, 0, report.text ( ), report.size ( ), bytes, sizeof ( bytes ), nullptr, nullptr );

			path.clear ( );
			path.add ( smoke_folder ).add ( L"\\pc_console.txt" );
			files::write ( path.text ( ), bytes, length );
			smoke_step = 1;

		} else if ( smoke_step == 1 )
			DestroyWindow ( window );

	}

	static LRESULT __stdcall procedure ( HWND handle, UINT message, WPARAM wide, LPARAM extra ) {

		if ( ( message >= WM_KEYFIRST && message <= WM_KEYLAST ) || ( message >= WM_MOUSEFIRST && message <= WM_MOUSELAST ) || message == WM_MOUSELEAVE )
			input_seen = true;

		switch ( message ) {

			case WM_ERASEBKGND:
				return 1;

			case WM_PAINT:
				ValidateRect ( handle, nullptr );
				render ( );
				return 0;

			case WM_SIZE:
				render ( );

				if ( captured )
					set_capture ( false );

				return 0;

			case WM_DPICHANGED: {

				const RECT* suggested = reinterpret_cast < const RECT* > ( extra );

				SetWindowPos ( handle, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE );
				render ( );

				return 0;

			}

			case WM_GETMINMAXINFO: {

				MINMAXINFO* limits = reinterpret_cast < MINMAXINFO* > ( extra );
				float factor = static_cast < float > ( GetDpiForWindow ( handle ) ) / 96.0f;

				limits->ptMinTrackSize.x = static_cast < LONG > ( 760.0f * factor );
				limits->ptMinTrackSize.y = static_cast < LONG > ( 520.0f * factor );

				return 0;

			}

			case WM_TIMER:

				if ( wide == smoke_timer )
					smoke_tick ( );

				return 0;

			case WM_APP + 1:
				toggle_fullscreen ( );
				return 0;

			case WM_APP + 2:
				control_alt_delete ( );
				return 0;

			case WM_APP + 3:
				take_screenshot ( );
				return 0;

			case WM_APP + 4:
				sent_ready ( reinterpret_cast < send_job* > ( extra ) );
				return 0;

			case WM_DROPFILES:
				dropped ( reinterpret_cast < HDROP > ( wide ) );
				return 0;

			case WM_CLIPBOARDUPDATE:
				share_host_clipboard ( );
				return 0;

			case WM_INPUT:

				if ( captured )
					raw_mouse ( extra );

				return DefWindowProcW ( handle, message, wide, extra );

			case WM_MOUSEMOVE: {

				float factor = scale ( );

				if ( !captured ) {

					TRACKMOUSEEVENT track = { sizeof ( TRACKMOUSEEVENT ), TME_LEAVE, handle, 0 };

					TrackMouseEvent ( &track );
					send_pointer ( extra, 0, pointer_buttons != 0 );
					frontend->mouse_move ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ) / factor, static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) / factor );

					if ( !running )
						render ( );

				}

				return 0;

			}

			case WM_MOUSELEAVE:
				frontend->mouse_leave ( );
				render ( );
				return 0;

			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
				pointer_button ( extra, 2, message == WM_RBUTTONDOWN );
				return 0;

			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
				pointer_button ( extra, 4, message == WM_MBUTTONDOWN );
				return 0;

			case WM_MOUSEWHEEL: {

				POINT spot = { static_cast < int16_t > ( LOWORD ( extra ) ), static_cast < int16_t > ( HIWORD ( extra ) ) };

				ScreenToClient ( handle, &spot );
				send_pointer ( MAKELPARAM ( spot.x, spot.y ), GET_WHEEL_DELTA_WPARAM ( wide ) / WHEEL_DELTA, false );

				return 0;

			}

			case WM_SETCURSOR:

				if ( LOWORD ( extra ) == HTCLIENT && integrated && running && !captured ) {

					POINT spot;
					float factor = scale ( );

					GetCursorPos ( &spot );
					ScreenToClient ( handle, &spot );

					if ( frontend->over_screen ( static_cast < float > ( spot.x ) / factor, static_cast < float > ( spot.y ) / factor ) ) {

						SetCursor ( nullptr );

						return TRUE;

					}

				}

				break;

			case WM_LBUTTONDOWN:

				if ( pointer_button ( extra, 1, true ) )
					return 0;

				if ( !captured ) {

					float factor = scale ( );

					SetCapture ( handle );
					frontend->mouse_down ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ) / factor, static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) / factor );
					render ( );

				}

				return 0;

			case WM_LBUTTONUP:

				if ( pointer_button ( extra, 1, false ) )
					return 0;

				if ( !captured ) {

					float factor = scale ( );

					ReleaseCapture ( );

					ui::console_action action = frontend->mouse_up ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ) / factor, static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) / factor );

					render ( );
					act ( action );

				}

				return 0;

			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYUP: {

				bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
				bool extended = ( extra >> 24 & 1 ) != 0;
				uint32_t scan = static_cast < uint32_t > ( extra >> 16 ) & 0xFF;

				if ( message == WM_SYSKEYDOWN && wide == VK_F4 && !captured )
					break;

				if ( host_key ( wide, extended, down ) )
					return 0;

				send_key ( static_cast < UINT > ( wide ), scan, extended, down );

				return 0;

			}

			case WM_SYSCHAR:
			case WM_CHAR:
				return 0;

			case WM_SETTINGCHANGE:

				if ( extra && base::same_text ( reinterpret_cast < const wchar_t* > ( extra ), L"ImmersiveColorSet" ) ) {

					apply_theme ( );
					render ( );

				}

				break;

			case WM_ACTIVATE:

				if ( LOWORD ( wide ) == WA_INACTIVE ) {

					set_capture ( false );
					release_keys ( );
					host_down = false;

				}

				break;

			case WM_CLOSE:
				DestroyWindow ( handle );
				return 0;

			case WM_DESTROY: {

				WINDOWPLACEMENT place = { sizeof ( WINDOWPLACEMENT ) };

				if ( fullscreen )
					place = saved_place;
				else
					GetWindowPlacement ( handle, &place );

				set_capture ( false );

				if ( running )
					save_preview ( );

				stop_typing ( );

				if ( sender ) {

					WaitForSingleObject ( sender, INFINITE );
					CloseHandle ( sender );
					sender = nullptr;

				}

				machine->power_off ( );

				if ( sent_disc [ 0 ] )
					DeleteFileW ( sent_disc );

				if ( clipboard_inbox [ 0 ] )
					remove_tree ( clipboard_inbox );

				RemoveClipboardFormatListener ( handle );

				running = false;
				file->window_x = place.rcNormalPosition.left;
				file->window_y = place.rcNormalPosition.top;
				file->window_width = place.rcNormalPosition.right - place.rcNormalPosition.left;
				file->window_height = place.rcNormalPosition.bottom - place.rcNormalPosition.top;
				file->maximized = place.showCmd == SW_SHOWMAXIMIZED;
				save_machine ( );
				RemovePropW ( handle, vm::window_tag );
				files::settle ( );
				PostQuitMessage ( 0 );

				return 0;

			}

			default:
				break;

		}

		return DefWindowProcW ( handle, message, wide, extra );

	}

	static void pump_messages ( ) {

		MSG message;
		bool quit = false;

		while ( !quit ) {

			HANDLE waits [ 1 ] = { };
			DWORD count = 0;
			DWORD timeout = INFINITE;
			HANDLE latency = static_cast < HANDLE > ( display->frame_waiter ( ) );
			bool timed = false;

			if ( running ) {

				double now = now_seconds ( );

				if ( latency && !slot_owed ) {

					waits [ 0 ] = latency;
					count = 1;

				} else if ( pacer ) {

					double period = latency || !run->frame_rate ? refresh_period : 1.0 / static_cast < double > ( run->frame_rate );

					if ( next_frame < now - period )
						next_frame = now;

					LARGE_INTEGER due;

					due.QuadPart = -static_cast < LONGLONG > ( base::max ( next_frame - now, 0.0 ) * 10000000.0 ) - 1;
					SetWaitableTimer ( pacer, &due, 0, nullptr, nullptr, FALSE );
					waits [ 0 ] = pacer;
					count = 1;
					timed = true;

				} else
					timeout = 8;

			}

			DWORD woke = MsgWaitForMultipleObjectsEx ( count, waits, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE );

			while ( PeekMessageW ( &message, nullptr, 0, 0, PM_REMOVE ) ) {

				if ( message.message == WM_QUIT ) {

					quit = true;

					break;

				}

				TranslateMessage ( &message );
				DispatchMessageW ( &message );

			}

			if ( quit || !running || ( count && woke != WAIT_OBJECT_0 ) || ( !count && woke != WAIT_TIMEOUT ) )
				continue;

			if ( timed )
				next_frame += latency || !run->frame_rate ? refresh_period : 1.0 / static_cast < double > ( run->frame_rate );

			bool presented = tick ( );

			if ( latency && !timed && !presented )
				slot_owed = true;

		}

	}

	void computer_record ( const wchar_t* path, double seconds, double from ) {

		copy_wide ( record_path, path, MAX_PATH );
		record_seconds = seconds;
		record_from = from;

	}

	int computer_main ( const wchar_t* path, vm::config& machine_file, const settings& theme, const wchar_t* smoke_path, const vm::launch_choice& choice ) {

		QueryPerformanceFrequency ( &frequency );
		file = &machine_file;
		run = new vm::config ( machine_file );
		vm::apply_launch ( choice, *run );
		file_path = path;
		global = &theme;
		smoke = smoke_path != nullptr;

		if ( smoke )
			copy_wide ( smoke_folder, smoke_path, MAX_PATH );

		HINSTANCE instance = GetModuleHandleW ( nullptr );
		WNDCLASSEXW type = { };

		type.cbSize = sizeof ( type );
		type.lpfnWndProc = &procedure;
		type.hInstance = instance;
		type.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
		type.hIcon = LoadIconW ( instance, MAKEINTRESOURCEW ( 1 ) );
		type.lpszClassName = L"coldboot_pc";
		RegisterClassExW ( &type );

		machine = new pc::computer ( );
		speaker.output = new audio ( );

		if ( !smoke )
			speaker.output->start ( );

		machine->speaker = &speaker;
		frontend = new ui::console ( );
		display = new render::gpu ( );
		frame = static_cast < uint32_t* > ( runtime_allocate_zero ( sizeof ( uint32_t ) * pc::vga::max_width * pc::vga::max_height ) );

		base::writer title;

		title.add ( machine_file.name ).add ( L" - Coldboot" );
		window = CreateWindowExW ( 0, L"coldboot_pc", title.text ( ), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 860, nullptr, nullptr, instance, nullptr );

		if ( !window )
			return 1;

		SetPropW ( window, vm::window_tag, reinterpret_cast < HANDLE > ( vm::running_hash ( path ) ) );
		DragAcceptFiles ( window, TRUE );

		if ( !smoke )
			AddClipboardFormatListener ( window );

		{

			wchar_t temporary [ MAX_PATH ] = { };
			base::writer inbox;

			GetTempPathW ( MAX_PATH, temporary );
			inbox.add ( temporary ).add ( L"Coldboot clipboard" );
			CreateDirectoryW ( inbox.text ( ), nullptr );
			inbox.add ( L"\\" ).number ( static_cast < int32_t > ( GetCurrentProcessId ( ) ) );
			copy_wide ( clipboard_inbox, inbox.text ( ), MAX_PATH );

		}

		float factor = scale ( );
		RECT bounds = { 0, 0, static_cast < LONG > ( 1060.0f * factor ), static_cast < LONG > ( 860.0f * factor ) };

		AdjustWindowRectExForDpi ( &bounds, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow ( window ) );

		RECT wanted = { machine_file.window_x, machine_file.window_y, machine_file.window_x + machine_file.window_width, machine_file.window_y + machine_file.window_height };

		if ( !smoke && machine_file.window_width > 200 && machine_file.window_height > 200 && MonitorFromRect ( &wanted, MONITOR_DEFAULTTONULL ) )
			SetWindowPos ( window, nullptr, wanted.left, wanted.top, machine_file.window_width, machine_file.window_height, SWP_NOZORDER | SWP_NOACTIVATE );
		else
			SetWindowPos ( window, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );

		apply_theme ( );

		RECT client;

		GetClientRect ( window, &client );
		display->paced = run->frame_rate == 0;
		display->synced = run->frame_rate == 0;
		refresh_period = read_refresh ( );
		pacer = CreateWaitableTimerExW ( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );

		if ( !display->create ( window, client.right, client.bottom ) ) {

			MessageBoxW ( window, L"Coldboot needs Direct3D 11 and couldn't start it on this PC.", L"Coldboot", MB_ICONERROR );

			return 1;

		}

		display->set_dpi ( 96.0f * factor );

		if ( smoke ) {

			smoke_started = now_seconds ( );
			SetTimer ( window, smoke_timer, 100, nullptr );

		} else
			ShowWindow ( window, machine_file.maximized ? SW_SHOWMAXIMIZED : SW_SHOW );

		power_on ( );
		pump_messages ( );

		if ( pacer )
			CloseHandle ( pacer );

		pacer = nullptr;
		delete machine;
		speaker.output->stop ( );
		delete speaker.output;
		speaker.output = nullptr;
		delete display;
		delete frontend;
		runtime_release ( frame );
		machine = nullptr;
		delete run;
		run = nullptr;

		return 0;

	}

}