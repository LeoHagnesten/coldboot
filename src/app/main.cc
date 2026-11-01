#include "app/keys.hh"
#include "app/session.hh"
#include "app/settings.hh"
#include "base/base.hh"
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#ifdef COLDBOOT_DEBUG

bool test_main ( int32_t& result );

#endif

struct theme_colors {

	COLORREF background;
	COLORREF screen;
	COLORREF bar;
	COLORREF line;
	COLORREF text;
	COLORREF muted;

};

static app::session* running = nullptr;
static uint32_t* frame = nullptr;
static bool light_theme = false;
static bool held [ 128 ] = { };

static theme_colors colors ( ) {

	if ( light_theme )
		return { RGB ( 0xF3, 0xF3, 0xF3 ), RGB ( 0x0C, 0x0C, 0x0C ), RGB ( 0xE8, 0xE8, 0xE8 ), RGB ( 0xD4, 0xD4, 0xD4 ), RGB ( 0x1E, 0x1E, 0x1E ), RGB ( 0x61, 0x61, 0x61 ) };

	return { RGB ( 0x1F, 0x1F, 0x1F ), RGB ( 0x0C, 0x0C, 0x0C ), RGB ( 0x18, 0x18, 0x18 ), RGB ( 0x2B, 0x2B, 0x2B ), RGB ( 0xCC, 0xCC, 0xCC ), RGB ( 0x9D, 0x9D, 0x9D ) };

}

static int32_t scaled ( HWND window, int32_t value ) {

	return MulDiv ( value, static_cast < int32_t > ( GetDpiForWindow ( window ) ), 96 );

}

static HFONT make_font ( HWND window, int32_t points, int32_t weight ) {

	return CreateFontW ( -MulDiv ( points, static_cast < int32_t > ( GetDpiForWindow ( window ) ), 72 ), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI" );

}

static void apply_theme ( HWND window ) {

	BOOL dark = light_theme ? FALSE : TRUE;

	DwmSetWindowAttribute ( window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof ( dark ) );

}

static RECT status_area ( HWND window, const RECT& client ) {

	RECT area = client;

	area.top = client.bottom - scaled ( window, 26 );

	return area;

}

static RECT screen_area ( HWND window, const RECT& client ) {

	int32_t margin = scaled ( window, 16 );
	int32_t room_width = client.right - margin * 2;
	int32_t room_height = status_area ( window, client ).top - margin * 2;
	int32_t width = room_width;
	int32_t height = width * 3 / 4;

	if ( height > room_height ) {

		height = room_height;
		width = height * 4 / 3;

	}

	RECT area;

	area.left = ( client.right - width ) / 2;
	area.top = margin + ( room_height - height ) / 2;
	area.right = area.left + base::max ( width, 0 );
	area.bottom = area.top + base::max ( height, 0 );

	return area;

}

static void fill ( HDC dc, const RECT& area, COLORREF color ) {

	HBRUSH brush = CreateSolidBrush ( color );

	FillRect ( dc, &area, brush );
	DeleteObject ( brush );

}

static void dot ( HDC dc, int32_t x, int32_t y, int32_t size, COLORREF color ) {

	HBRUSH brush = CreateSolidBrush ( color );
	HGDIOBJ previous_brush = SelectObject ( dc, brush );
	HGDIOBJ previous_pen = SelectObject ( dc, GetStockObject ( NULL_PEN ) );

	Ellipse ( dc, x, y, x + size + 1, y + size + 1 );
	SelectObject ( dc, previous_pen );
	SelectObject ( dc, previous_brush );
	DeleteObject ( brush );

}

static int32_t text_width ( HDC dc, const wchar_t* text ) {

	SIZE extent = { };

	GetTextExtentPoint32W ( dc, text, base::text_length ( text ), &extent );

	return extent.cx;

}

static void draw_drives ( HWND window, HDC dc, RECT area, const theme_colors& palette ) {

	int32_t gap = scaled ( window, 18 );
	int32_t size = scaled ( window, 6 );

	for ( int32_t i = app::session::drive_count - 1; i >= 0; i-- ) {

		wchar_t name [ MAX_PATH ];

		if ( !running || !running->disk_name ( i, name, MAX_PATH ) )
			continue;

		base::writer label;

		label.add ( i ? L"B:  " : L"A:  " ).add ( name );

		bool lit = running->light ( i );
		int32_t width = text_width ( dc, label.text ( ) );

		SetTextColor ( dc, lit ? palette.text : palette.muted );
		DrawTextW ( dc, label.text ( ), -1, &area, DT_RIGHT | DT_VCENTER | DT_SINGLELINE );
		area.right -= width + scaled ( window, 7 ) + size;
		dot ( dc, area.right, ( area.top + area.bottom - size ) / 2, size, lit ? RGB ( 0xE8, 0x3B, 0x2F ) : palette.line );
		area.right -= gap;

	}

}

static void draw_message ( HWND window, HDC dc, const RECT& area, const wchar_t* title, const wchar_t* detail ) {

	HFONT heading = make_font ( window, 13, FW_SEMIBOLD );
	HFONT body = make_font ( window, 10, FW_NORMAL );
	RECT top = area;
	RECT bottom = area;
	int32_t middle = ( area.top + area.bottom ) / 2;

	top.bottom = middle - scaled ( window, 4 );
	bottom.top = middle + scaled ( window, 4 );
	SetTextColor ( dc, RGB ( 0xE6, 0xE6, 0xE6 ) );

	HGDIOBJ previous = SelectObject ( dc, heading );

	DrawTextW ( dc, title, -1, &top, DT_CENTER | DT_BOTTOM | DT_SINGLELINE );
	SelectObject ( dc, body );
	SetTextColor ( dc, RGB ( 0x9D, 0x9D, 0x9D ) );
	DrawTextW ( dc, detail, -1, &bottom, DT_CENTER | DT_TOP | DT_SINGLELINE );
	SelectObject ( dc, previous );
	DeleteObject ( heading );
	DeleteObject ( body );

}

static void paint ( HWND window ) {

	PAINTSTRUCT paint;
	HDC target = BeginPaint ( window, &paint );
	RECT client;

	GetClientRect ( window, &client );

	HDC dc = CreateCompatibleDC ( target );
	HBITMAP surface = CreateCompatibleBitmap ( target, client.right, client.bottom );
	HGDIOBJ previous_surface = SelectObject ( dc, surface );
	theme_colors palette = colors ( );
	RECT screen = screen_area ( window, client );
	RECT bar = status_area ( window, client );
	int32_t width = 0;
	int32_t height = 0;

	fill ( dc, client, palette.background );
	fill ( dc, screen, palette.screen );
	SetBkMode ( dc, TRANSPARENT );

	if ( running && running->latest ( frame, width, height ) ) {

		BITMAPINFO info = { };

		info.bmiHeader.biSize = sizeof ( info.bmiHeader );
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		SetStretchBltMode ( dc, HALFTONE );
		SetBrushOrgEx ( dc, 0, 0, nullptr );
		StretchDIBits ( dc, screen.left, screen.top, screen.right - screen.left, screen.bottom - screen.top, 0, 0, width, height, frame, &info, DIB_RGB_COLORS, SRCCOPY );

	} else if ( running && running->state == app::SESSION_FAILED )
		draw_message ( window, dc, screen, L"That file can't be used as a BIOS", L"Use a raw ROM image of 64 KB or less, or a floppy image from 160 KB to 1.44 MB." );
	else if ( !running || running->state != app::SESSION_RUNNING )
		draw_message ( window, dc, screen, L"No BIOS loaded", L"Drop a BIOS ROM onto this window to power on. Floppy images go in drive A, or B if you hold Shift." );

	fill ( dc, bar, palette.bar );

	RECT line = bar;

	line.bottom = line.top + 1;
	fill ( dc, line, palette.line );

	HFONT font = make_font ( window, 9, FW_NORMAL );
	HGDIOBJ previous_font = SelectObject ( dc, font );
	RECT text = bar;

	text.left += scaled ( window, 12 );
	text.right -= scaled ( window, 12 );
	SetTextColor ( dc, palette.muted );
	DrawTextW ( dc, L"IBM PC/XT  \x00B7  8088 at 4.77 MHz  \x00B7  640 KB  \x00B7  CGA", -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

	const wchar_t* status = L"Off";

	if ( running && running->state == app::SESSION_RUNNING )
		status = L"Running";
	else if ( running && running->state == app::SESSION_FAILED )
		status = L"Couldn't load the BIOS";

	SetTextColor ( dc, palette.text );
	DrawTextW ( dc, status, -1, &text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE );

	RECT drives = text;

	drives.right -= text_width ( dc, status ) + scaled ( window, 24 );
	draw_drives ( window, dc, drives, palette );
	SelectObject ( dc, previous_font );
	DeleteObject ( font );
	BitBlt ( target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY );
	SelectObject ( dc, previous_surface );
	DeleteObject ( surface );
	DeleteDC ( dc );
	EndPaint ( window, &paint );

}

static void release_keys ( ) {

	for ( int32_t scan = 0; scan < 128; scan++ ) {

		if ( !held [ scan ] )
			continue;

		held [ scan ] = false;

		if ( running )
			running->key ( static_cast < uint8_t > ( scan | 0x80 ) );

	}

}

static void power_on ( HWND window, const wchar_t* path, bool remember ) {

	release_keys ( );

	if ( running->start ( path, window ) && remember )
		app::remember_bios ( path );

	InvalidateRect ( window, nullptr, FALSE );

}

static bool is_disk ( const wchar_t* path ) {

	WIN32_FILE_ATTRIBUTE_DATA data;

	if ( !GetFileAttributesExW ( path, GetFileExInfoStandard, &data ) || ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) || data.nFileSizeHigh )
		return false;

	return machine::floppy::identify ( data.nFileSizeLow ) != nullptr;

}

static void open_files ( HWND window, const wchar_t* const* paths, int32_t count, int32_t first_drive, bool remember ) {

	int32_t placed = 0;
	const wchar_t* bios = nullptr;

	for ( int32_t i = 0; i < count; i++ ) {

		if ( !is_disk ( paths [ i ] ) )
			bios = paths [ i ];
		else if ( placed < app::session::drive_count && running->insert ( ( first_drive + placed ) % app::session::drive_count, paths [ i ] ) )
			placed++;

	}

	if ( bios )
		power_on ( window, bios, remember );
	else
		InvalidateRect ( window, nullptr, FALSE );

}

static LRESULT __stdcall procedure ( HWND window, UINT message, WPARAM wide, LPARAM extra ) {

	switch ( message ) {

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
			paint ( window );
			return 0;

		case WM_SIZE:
			InvalidateRect ( window, nullptr, FALSE );
			return 0;

		case WM_DPICHANGED: {

			const RECT* suggested = reinterpret_cast < const RECT* > ( extra );

			SetWindowPos ( window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE );

			return 0;

		}

		case app::session::frame_message: {

			RECT client;

			GetClientRect ( window, &client );

			RECT screen = screen_area ( window, client );

			InvalidateRect ( window, &screen, FALSE );

			return 0;

		}

		case app::session::light_message: {

			RECT client;

			GetClientRect ( window, &client );

			RECT bar = status_area ( window, client );

			InvalidateRect ( window, &bar, FALSE );

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
			open_files ( window, paths, count, GetAsyncKeyState ( VK_SHIFT ) & 0x8000 ? 1 : 0, true );

			return 0;

		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP: {

			if ( message == WM_SYSKEYDOWN && wide == VK_F4 )
				break;

			int32_t scan = app::xt_scan_code ( wide, extra );
			bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;

			if ( scan < 0 || !running || running->state != app::SESSION_RUNNING )
				return 0;

			held [ scan ] = down;
			running->key ( static_cast < uint8_t > ( down ? scan : scan | 0x80 ) );

			return 0;

		}

		case WM_SYSCHAR:
			return 0;

		case WM_KILLFOCUS:
			release_keys ( );
			return 0;

		case WM_DESTROY:
			running->stop ( );
			PostQuitMessage ( 0 );
			return 0;

		default:
			break;

	}

	return DefWindowProcW ( window, message, wide, extra );

}

static bool open_command_line ( HWND window ) {

	int32_t count = 0;
	wchar_t** arguments = CommandLineToArgvW ( GetCommandLineW ( ), &count );
	const wchar_t* paths [ 4 ];
	int32_t found = 0;
	bool bios = false;

	for ( int32_t i = 1; arguments && i < count && found < 4; i++ ) {

		if ( arguments [ i ] [ 0 ] == L'-' )
			continue;

		bios = bios || !is_disk ( arguments [ i ] );
		paths [ found++ ] = arguments [ i ];

	}

	open_files ( window, paths, found, 0, false );

	if ( arguments )
		LocalFree ( arguments );

	return bios;

}

int program_main ( ) {

#ifdef COLDBOOT_DEBUG

	int32_t tested = 0;

	if ( test_main ( tested ) )
		return tested;

#endif

	SetProcessDpiAwarenessContext ( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

	HINSTANCE instance = GetModuleHandleW ( nullptr );
	WNDCLASSEXW type = { };

	type.cbSize = sizeof ( type );
	type.lpfnWndProc = &procedure;
	type.hInstance = instance;
	type.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
	type.hIcon = LoadIconW ( instance, MAKEINTRESOURCEW ( 1 ) );
	type.lpszClassName = L"coldboot";
	RegisterClassExW ( &type );

	running = new app::session ( );
	frame = static_cast < uint32_t* > ( runtime_allocate_zero ( sizeof ( uint32_t ) * machine::cga::max_width * machine::cga::max_height ) );

	HWND window = CreateWindowExW ( 0, L"coldboot", L"Coldboot", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 860, nullptr, nullptr, instance, nullptr );

	if ( !window )
		return 1;

	RECT bounds = { 0, 0, scaled ( window, 992 ), scaled ( window, 778 ) };

	AdjustWindowRectExForDpi ( &bounds, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow ( window ) );
	SetWindowPos ( window, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
	apply_theme ( window );
	DragAcceptFiles ( window, TRUE );
	ShowWindow ( window, SW_SHOW );

	wchar_t path [ MAX_PATH ] = { };

	if ( !open_command_line ( window ) && app::remembered_bios ( path, MAX_PATH ) && GetFileAttributesW ( path ) != INVALID_FILE_ATTRIBUTES )
		power_on ( window, path, false );

	MSG message;

	while ( GetMessageW ( &message, nullptr, 0, 0 ) > 0 ) {

		TranslateMessage ( &message );
		DispatchMessageW ( &message );

	}

	delete running;
	runtime_release ( frame );

	return 0;

}