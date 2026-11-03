#include <winsock2.h>
#include <ws2tcpip.h>
#include "app/clipboard.hh"
#include "pc/tools.hh"
#include <shellapi.h>
#include <shlobj.h>

namespace app {

	static pc::guest_tools* bridge = nullptr;
	static HWND agent_window = nullptr;
	static DWORD ignore_sequence = 0;
	static volatile long agent_quit = 0;
	static wchar_t agent_inbox [ MAX_PATH ] = { };

	static bool starts_with ( const wchar_t* text, const wchar_t* prefix ) {

		for ( ; *prefix; text++, prefix++ ) {

			if ( ( *text | 0x20 ) != ( *prefix | 0x20 ) )
				return false;

		}

		return true;

	}

	bool tools_wanted ( wchar_t** arguments, int32_t count ) {

		for ( int32_t i = 1; arguments && i < count; i++ ) {

			if ( base::same_text ( arguments [ i ], L"--agent" ) || base::same_text ( arguments [ i ], L"--install" ) )
				return true;

		}

		wchar_t self [ MAX_PATH ] = { };
		const wchar_t* leaf = self;

		GetModuleFileNameW ( nullptr, self, MAX_PATH );

		for ( const wchar_t* at = self; *at; at++ ) {

			if ( *at == L'\\' )
				leaf = at + 1;

		}

		return starts_with ( leaf, L"Coldboot Tools" );

	}

	static bool installed_path ( wchar_t* folder, wchar_t* program ) {

		wchar_t* local = nullptr;

		if ( FAILED ( SHGetKnownFolderPath ( FOLDERID_LocalAppData, 0, nullptr, &local ) ) )
			return false;

		base::writer path;

		path.add ( local ).add ( L"\\Coldboot Tools" );
		CoTaskMemFree ( local );

		for ( int32_t i = 0; i <= path.size ( ); i++ )
			folder [ i ] = path.text ( ) [ i ];

		path.add ( L"\\Coldboot Tools.exe" );

		for ( int32_t i = 0; i <= path.size ( ); i++ )
			program [ i ] = path.text ( ) [ i ];

		return true;

	}

	static int32_t install_tools ( bool quiet ) {

		wchar_t self [ MAX_PATH ] = { };
		wchar_t folder [ MAX_PATH ] = { };
		wchar_t program [ MAX_PATH ] = { };

		GetModuleFileNameW ( nullptr, self, MAX_PATH );

		if ( !installed_path ( folder, program ) ) {

			MessageBoxW ( nullptr, L"Coldboot Tools couldn't find a place to install.", L"Coldboot Tools", MB_ICONERROR );

			return 1;

		}

		CreateDirectoryW ( folder, nullptr );

		bool same = base::same_text ( self, program );
		HWND older = same ? nullptr : FindWindowExW ( HWND_MESSAGE, nullptr, L"coldboot_tools", nullptr );

		if ( older ) {

			DWORD owner = 0;

			GetWindowThreadProcessId ( older, &owner );

			HANDLE process = owner ? OpenProcess ( SYNCHRONIZE, FALSE, owner ) : nullptr;

			PostMessageW ( older, WM_CLOSE, 0, 0 );

			if ( process ) {

				WaitForSingleObject ( process, 15000 );
				CloseHandle ( process );

			}

		}

		bool copied = same;

		for ( int32_t attempt = 0; !copied && attempt < 50; attempt++ ) {

			SetFileAttributesW ( program, FILE_ATTRIBUTE_NORMAL );
			copied = CopyFileW ( self, program, FALSE ) != 0;

			if ( !copied )
				Sleep ( 100 );

		}

		if ( copied && !same )
			SetFileAttributesW ( program, FILE_ATTRIBUTE_NORMAL );

		HANDLE running = copied ? nullptr : OpenMutexW ( SYNCHRONIZE, FALSE, L"Local\\coldboot_tools_agent" );

		copied = copied || running;

		if ( running )
			CloseHandle ( running );

		if ( !copied ) {

			MessageBoxW ( nullptr, L"Coldboot Tools couldn't copy itself into your user folder.", L"Coldboot Tools", MB_ICONERROR );

			return 1;

		}

		base::writer command;

		command.add ( L"\"" ).add ( program ).add ( L"\" --agent" );
		RegSetKeyValueW ( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Coldboot Tools", REG_SZ, command.text ( ), static_cast < DWORD > ( ( command.size ( ) + 1 ) * sizeof ( wchar_t ) ) );
		ShellExecuteW ( nullptr, L"open", program, L"--agent", folder, SW_HIDE );

		if ( !quiet )
			MessageBoxW ( nullptr, L"Coldboot Tools are installed and running, and they start with Windows from now on.\n\nCopy and paste between this machine and your PC now works for text and files, in whichever directions the machine's settings allow.", L"Coldboot Tools", MB_ICONINFORMATION );

		return 0;

	}

	static DWORD __stdcall network_loop ( void* ) {

		uint8_t* buffer = static_cast < uint8_t* > ( runtime_allocate ( 0x10000 ) );

		while ( !agent_quit ) {

			SOCKET handle = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );
			sockaddr_in place = { };

			place.sin_family = AF_INET;
			place.sin_port = htons ( net::nat::tools_port );
			place.sin_addr.s_addr = htonl ( net::nat::tools_address );

			if ( handle == INVALID_SOCKET || connect ( handle, reinterpret_cast < sockaddr* > ( &place ), sizeof ( place ) ) != 0 ) {

				if ( handle != INVALID_SOCKET )
					closesocket ( handle );

				for ( int32_t i = 0; i < 30 && !agent_quit; i++ )
					Sleep ( 100 );

				continue;

			}

			BOOL quick = TRUE;

			setsockopt ( handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast < const char* > ( &quick ), sizeof ( quick ) );
			bridge->opened ( );

			bool alive = true;

			while ( alive && !agent_quit ) {

				for ( ;; ) {

					uint32_t got = bridge->produce ( buffer, 0x10000 );

					if ( !got )
						break;

					for ( uint32_t done = 0; done < got && alive; ) {

						int sent = send ( handle, reinterpret_cast < const char* > ( buffer + done ), static_cast < int > ( got - done ), 0 );

						if ( sent <= 0 )
							alive = false;
						else
							done += static_cast < uint32_t > ( sent );

					}

					if ( !alive )
						break;

				}

				fd_set readable;
				timeval wait = { 0, 20000 };

				FD_ZERO ( &readable );
				FD_SET ( handle, &readable );

				if ( alive && select ( 0, &readable, nullptr, nullptr, &wait ) > 0 ) {

					int got = recv ( handle, reinterpret_cast < char* > ( buffer ), 0x10000, 0 );

					if ( got <= 0 )
						alive = false;
					else
						bridge->received ( buffer, static_cast < uint32_t > ( got ) );

				}

			}

			bridge->closed ( );
			closesocket ( handle );

			for ( int32_t i = 0; i < 10 && !agent_quit; i++ )
				Sleep ( 100 );

		}

		runtime_release ( buffer );

		return 0;

	}

	static void share_clipboard ( ) {

		DWORD sequence = GetClipboardSequenceNumber ( );

		if ( sequence == ignore_sequence || !bridge->connected ( ) )
			return;

		uint32_t policy = bridge->current_policy ( );

		if ( clipboard_has_files ( ) ) {

			if ( !( policy & pc::SHARE_FILES_OUT ) )
				return;

			wchar_t ( *paths ) [ MAX_PATH ] = static_cast < wchar_t ( * ) [ MAX_PATH ] > ( runtime_allocate_zero ( sizeof ( wchar_t ) * MAX_PATH * 64 ) );
			int32_t count = read_clipboard_files ( agent_window, paths, 64 );

			if ( count )
				bridge->send_files ( paths, count, pc::PURPOSE_CLIPBOARD, pc::clipboard_file_limit );

			runtime_release ( paths );

			return;

		}

		if ( clipboard_has_text ( ) && ( policy & pc::SHARE_TEXT_OUT ) ) {

			base::list < wchar_t > text;

			if ( read_clipboard_text ( agent_window, text ) )
				bridge->send_text ( text.data ( ), static_cast < uint32_t > ( text.size ( ) ) );

		}

	}

	static void deliver_sent ( const wchar_t* items ) {

		wchar_t* desktop = nullptr;

		if ( FAILED ( SHGetKnownFolderPath ( FOLDERID_Desktop, 0, nullptr, &desktop ) ) )
			return;

		base::writer folder;

		folder.add ( desktop ).add ( L"\\From your PC" );
		CoTaskMemFree ( desktop );
		CreateDirectoryW ( folder.text ( ), nullptr );

		for ( const wchar_t* item = items; *item; item += base::text_length ( item ) + 1 ) {

			const wchar_t* leaf = item;

			for ( const wchar_t* at = item; *at; at++ ) {

				if ( *at == L'\\' )
					leaf = at + 1;

			}

			for ( int32_t copy = 1; copy < 100; copy++ ) {

				base::writer target;

				target.add ( folder.text ( ) ).add ( L"\\" ).add ( leaf );

				if ( copy > 1 )
					target.add ( L" (" ).number ( copy ).add ( L")" );

				if ( MoveFileExW ( item, target.text ( ), MOVEFILE_COPY_ALLOWED ) || GetLastError ( ) != ERROR_ALREADY_EXISTS )
					break;

			}

		}

		ShellExecuteW ( nullptr, L"open", folder.text ( ), nullptr, nullptr, SW_SHOWNORMAL );

	}

	static void drain_events ( ) {

		pc::tools_event item;

		while ( bridge->next_event ( item ) ) {

			if ( item.kind == pc::TOOLS_GOT_TEXT && !item.text.empty ( ) )
				ignore_sequence = write_clipboard_text ( agent_window, item.text.data ( ) );
			else if ( item.kind == pc::TOOLS_GOT_FILES && !item.items.empty ( ) ) {

				item.items.push ( 0 );

				if ( item.purpose == pc::PURPOSE_SENT )
					deliver_sent ( item.items.data ( ) );
				else
					ignore_sequence = write_clipboard_files ( agent_window, item.items.data ( ) );

			}

		}

	}

	static LRESULT __stdcall agent_procedure ( HWND handle, UINT message, WPARAM wide, LPARAM extra ) {

		switch ( message ) {

			case WM_CLIPBOARDUPDATE:
				share_clipboard ( );
				return 0;

			case WM_TIMER:
				drain_events ( );
				return 0;

			case WM_DESTROY:
				PostQuitMessage ( 0 );
				return 0;

			default:
				return DefWindowProcW ( handle, message, wide, extra );

		}

	}

	int32_t tools_main ( wchar_t** arguments, int32_t count ) {

		bool agent = false;
		bool quiet = false;

		for ( int32_t i = 1; arguments && i < count; i++ ) {

			agent = agent || base::same_text ( arguments [ i ], L"--agent" );
			quiet = quiet || base::same_text ( arguments [ i ], L"--install" );

		}

		if ( !agent )
			return install_tools ( quiet );

		HANDLE single = CreateMutexW ( nullptr, TRUE, L"Local\\coldboot_tools_agent" );

		if ( !single || GetLastError ( ) == ERROR_ALREADY_EXISTS )
			return 0;

		WSADATA data;

		if ( WSAStartup ( MAKEWORD ( 2, 2 ), &data ) )
			return 1;

		bridge = new pc::guest_tools ( );
		bridge->agent = true;
		GetTempPathW ( MAX_PATH, agent_inbox );

		base::writer inbox;

		inbox.add ( agent_inbox ).add ( L"Coldboot Tools" );
		remove_tree ( inbox.text ( ) );
		CreateDirectoryW ( inbox.text ( ), nullptr );
		bridge->set_inbox ( inbox.text ( ) );

		WNDCLASSEXW type = { };

		type.cbSize = sizeof ( type );
		type.lpfnWndProc = &agent_procedure;
		type.hInstance = GetModuleHandleW ( nullptr );
		type.lpszClassName = L"coldboot_tools";
		RegisterClassExW ( &type );
		agent_window = CreateWindowExW ( 0, L"coldboot_tools", L"Coldboot Tools", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, nullptr );

		if ( !agent_window )
			return 1;

		AddClipboardFormatListener ( agent_window );
		SetTimer ( agent_window, 1, 50, nullptr );

		HANDLE network = CreateThread ( nullptr, 0, &network_loop, nullptr, 0, nullptr );
		MSG message;

		while ( GetMessageW ( &message, nullptr, 0, 0 ) > 0 ) {

			TranslateMessage ( &message );
			DispatchMessageW ( &message );

		}

		InterlockedExchange ( &agent_quit, 1 );

		if ( network )
			WaitForSingleObject ( network, 2000 );

		CloseHandle ( single );

		return 0;

	}

}