#include "app/clipboard.hh"
#include <shellapi.h>
#include <shlobj.h>

namespace app {

	static bool open_with_retry ( HWND owner ) {

		for ( int32_t attempt = 0; attempt < 10; attempt++ ) {

			if ( OpenClipboard ( owner ) )
				return true;

			Sleep ( 10 );

		}

		return false;

	}

	bool clipboard_has_files ( ) {

		return IsClipboardFormatAvailable ( CF_HDROP ) != FALSE;

	}

	bool clipboard_has_text ( ) {

		return IsClipboardFormatAvailable ( CF_UNICODETEXT ) != FALSE;

	}

	bool read_clipboard_text ( HWND owner, base::list < wchar_t >& out ) {

		out.clear ( );

		if ( !open_with_retry ( owner ) )
			return false;

		HANDLE data = GetClipboardData ( CF_UNICODETEXT );
		const wchar_t* text = data ? static_cast < const wchar_t* > ( GlobalLock ( data ) ) : nullptr;

		if ( text ) {

			size_t limit = GlobalSize ( data ) / sizeof ( wchar_t );

			for ( size_t i = 0; i < limit && text [ i ] && i < 0x400000; i++ )
				out.push ( text [ i ] );

			GlobalUnlock ( data );

		}

		CloseClipboard ( );

		return !out.empty ( );

	}

	int32_t read_clipboard_files ( HWND owner, wchar_t ( *out ) [ MAX_PATH ], int32_t room ) {

		if ( !open_with_retry ( owner ) )
			return 0;

		HDROP drop = static_cast < HDROP > ( GetClipboardData ( CF_HDROP ) );
		int32_t count = 0;

		if ( drop ) {

			UINT total = DragQueryFileW ( drop, 0xFFFFFFFF, nullptr, 0 );

			for ( UINT i = 0; i < total && count < room; i++ ) {

				if ( DragQueryFileW ( drop, i, out [ count ], MAX_PATH ) )
					count++;

			}

		}

		CloseClipboard ( );

		return count;

	}

	DWORD write_clipboard_text ( HWND owner, const wchar_t* text ) {

		int32_t length = base::text_length ( text );
		HGLOBAL memory = GlobalAlloc ( GMEM_MOVEABLE, sizeof ( wchar_t ) * ( length + 1 ) );

		if ( !memory )
			return 0;

		wchar_t* target = static_cast < wchar_t* > ( GlobalLock ( memory ) );

		for ( int32_t i = 0; i <= length; i++ )
			target [ i ] = text [ i ];

		GlobalUnlock ( memory );

		if ( !open_with_retry ( owner ) ) {

			GlobalFree ( memory );

			return 0;

		}

		EmptyClipboard ( );

		if ( !SetClipboardData ( CF_UNICODETEXT, memory ) )
			GlobalFree ( memory );

		CloseClipboard ( );

		return GetClipboardSequenceNumber ( );

	}

	DWORD write_clipboard_files ( HWND owner, const wchar_t* items ) {

		size_t length = 0;

		while ( items [ length ] || items [ length + 1 ] )
			length++;

		length += 2;

		HGLOBAL memory = GlobalAlloc ( GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof ( DROPFILES ) + sizeof ( wchar_t ) * length );
		HGLOBAL effect = GlobalAlloc ( GMEM_MOVEABLE, sizeof ( DWORD ) );

		if ( !memory || !effect ) {

			if ( memory )
				GlobalFree ( memory );

			if ( effect )
				GlobalFree ( effect );

			return 0;

		}

		DROPFILES* header = static_cast < DROPFILES* > ( GlobalLock ( memory ) );
		wchar_t* list = reinterpret_cast < wchar_t* > ( header + 1 );

		header->pFiles = sizeof ( DROPFILES );
		header->fWide = TRUE;

		for ( size_t i = 0; i < length; i++ )
			list [ i ] = items [ i ];

		GlobalUnlock ( memory );
		*static_cast < DWORD* > ( GlobalLock ( effect ) ) = DROPEFFECT_COPY;
		GlobalUnlock ( effect );

		if ( !open_with_retry ( owner ) ) {

			GlobalFree ( memory );
			GlobalFree ( effect );

			return 0;

		}

		EmptyClipboard ( );

		if ( !SetClipboardData ( CF_HDROP, memory ) )
			GlobalFree ( memory );

		if ( !SetClipboardData ( RegisterClipboardFormatW ( CFSTR_PREFERREDDROPEFFECT ), effect ) )
			GlobalFree ( effect );

		CloseClipboard ( );

		return GetClipboardSequenceNumber ( );

	}

	void remove_tree ( const wchar_t* path ) {

		WIN32_FIND_DATAW found;
		base::writer pattern;

		pattern.add ( path ).add ( L"\\*" );

		HANDLE search = FindFirstFileW ( pattern.text ( ), &found );

		if ( search != INVALID_HANDLE_VALUE ) {

			do {

				if ( found.cFileName [ 0 ] == L'.' && ( !found.cFileName [ 1 ] || ( found.cFileName [ 1 ] == L'.' && !found.cFileName [ 2 ] ) ) )
					continue;

				base::writer child;

				child.add ( path ).add ( L"\\" ).add ( found.cFileName );

				if ( found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) {

					if ( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
						RemoveDirectoryW ( child.text ( ) );
					else
						DeleteFileW ( child.text ( ) );

				} else if ( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
					remove_tree ( child.text ( ) );
				else {

					SetFileAttributesW ( child.text ( ), FILE_ATTRIBUTE_NORMAL );
					DeleteFileW ( child.text ( ) );

				}

			} while ( FindNextFileW ( search, &found ) );

			FindClose ( search );

		}

		RemoveDirectoryW ( path );

	}

}