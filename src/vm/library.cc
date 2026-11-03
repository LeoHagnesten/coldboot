#include "vm/library.hh"
#include "base/files.hh"
#include <shlobj.h>

namespace vm {

	static void copy_text ( wchar_t* out, const wchar_t* text, int32_t room ) {

		int32_t length = 0;

		for ( ; text && text [ length ] && length < room - 1; length++ )
			out [ length ] = text [ length ];

		out [ length ] = 0;

	}

	static bool same_path ( const wchar_t* a, const wchar_t* b ) {

		return CompareStringOrdinal ( a, -1, b, -1, TRUE ) == CSTR_EQUAL;

	}

	static bool report ( char* out, int32_t room, const char* text ) {

		int32_t used = 0;

		for ( ; out && text [ used ] && used < room - 1; used++ )
			out [ used ] = text [ used ];

		if ( out && room > 0 )
			out [ used ] = 0;

		return false;

	}

	static bool write_atomic ( const wchar_t* path, const char* data, int32_t size ) {

		base::writer staging;

		staging.add ( path ).add ( L".new" );

		return files::write ( staging.text ( ), data, size ) && MoveFileExW ( staging.text ( ), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH );

	}

	bool library::load ( const wchar_t* list ) {

		base::list < char > data;

		copy_text ( list_path, list, MAX_PATH );
		entries.clear ( );

		if ( !files::read ( list, data ) )
			return false;

		const char* text = data.data ( );
		int32_t total = data.count ( ) - 1;
		int32_t start = 0;

		while ( start < total ) {

			int32_t end = start;

			while ( end < total && text [ end ] != '\n' )
				end++;

			int32_t size = end - start;

			if ( size && text [ end - 1 ] == '\r' )
				size--;

			wchar_t path [ MAX_PATH ];
			int32_t written = size ? MultiByteToWideChar ( CP_UTF8, MB_ERR_INVALID_CHARS, text + start, size, path, MAX_PATH - 1 ) : 0;

			if ( written > 0 ) {

				path [ written ] = 0;
				add ( path );

			}

			start = end + 1;

		}

		return true;

	}

	bool library::save ( ) const {

		base::list < char > text;

		for ( const entry& item : entries ) {

			char bytes [ MAX_PATH * 3 ];
			int32_t size = WideCharToMultiByte ( CP_UTF8, 0, item.path, -1, bytes, sizeof ( bytes ), nullptr, nullptr );

			for ( int32_t i = 0; i < size - 1; i++ )
				text.push ( bytes [ i ] );

			text.push ( '\n' );

		}

		return write_atomic ( list_path, text.data ( ), text.count ( ) );

	}

	int32_t library::find ( const wchar_t* path ) const {

		for ( int32_t i = 0; i < entries.count ( ); i++ ) {

			if ( same_path ( entries [ i ].path, path ) )
				return i;

		}

		return -1;

	}

	bool library::add ( const wchar_t* path ) {

		if ( !path || !path [ 0 ] || find ( path ) >= 0 )
			return false;

		entries.push ( entry ( ) );
		copy_text ( entries [ entries.count ( ) - 1 ].path, path, MAX_PATH );
		refresh ( entries.count ( ) - 1 );

		return true;

	}

	void library::remove ( int32_t index ) {

		if ( index < 0 || index >= entries.count ( ) )
			return;

		base::list < entry > kept;

		for ( int32_t i = 0; i < entries.count ( ); i++ ) {

			if ( i != index )
				kept.push ( entries [ i ] );

		}

		entries = base::move ( kept );

	}

	bool library::refresh ( int32_t index ) {

		if ( index < 0 || index >= entries.count ( ) )
			return false;

		entry& item = entries [ index ];

		item.problem [ 0 ] = 0;
		item.readable = vm::load ( item.path, item.settings, item.problem, sizeof ( item.problem ) );

		return item.readable;

	}

	bool library::create ( const config& settings, const wchar_t* base_folder, wchar_t* out_path, char* error, int32_t room, uint64_t disk_bytes ) {

		if ( !valid_name ( settings.name ) )
			return report ( error, room, "Pick a name without \\ / : * ? \" < > |" );

		if ( !CreateDirectoryW ( base_folder, nullptr ) && GetLastError ( ) != ERROR_ALREADY_EXISTS )
			return report ( error, room, "Couldn't create the folder for virtual machines" );

		base::writer folder;

		for ( int32_t attempt = 1; attempt < 100; attempt++ ) {

			folder.clear ( );
			folder.add ( base_folder ).add ( L"\\" ).add ( settings.name );

			if ( attempt > 1 )
				folder.add ( L" " ).number ( attempt );

			if ( CreateDirectoryW ( folder.text ( ), nullptr ) )
				break;

			if ( GetLastError ( ) != ERROR_ALREADY_EXISTS || attempt == 99 )
				return report ( error, room, "Couldn't create the machine's folder" );

		}

		base::writer path;
		config* stored = new config ( settings );

		path.add ( folder.text ( ) ).add ( L"\\" ).add ( settings.name ).add ( L".coldboot" );

		if ( disk_bytes && !stored->disk [ 0 ] ) {

			base::writer disk;

			disk.add ( folder.text ( ) ).add ( L"\\" ).add ( settings.name ).add ( L".img" );

			if ( !create_disk ( disk.text ( ), disk_bytes ) ) {

				delete stored;
				RemoveDirectoryW ( folder.text ( ) );

				return report ( error, room, "Couldn't create the virtual hard disk" );

			}

			copy_text ( stored->disk, disk.text ( ), MAX_PATH );

		}

		bool written = vm::save ( path.text ( ), *stored );

		if ( !written ) {

			if ( disk_bytes && stored->disk [ 0 ] && !settings.disk [ 0 ] )
				DeleteFileW ( stored->disk );

			delete stored;
			RemoveDirectoryW ( folder.text ( ) );

			return report ( error, room, "Couldn't write the machine file" );

		}

		delete stored;

		add ( path.text ( ) );

		if ( out_path )
			copy_text ( out_path, path.text ( ), MAX_PATH );

		return save ( ) || report ( error, room, "The machine was created but the library list couldn't be saved" );

	}

	bool default_folder ( wchar_t* out ) {

		wchar_t* documents = nullptr;

		if ( FAILED ( SHGetKnownFolderPath ( FOLDERID_Documents, 0, nullptr, &documents ) ) )
			return false;

		base::writer path;

		path.add ( documents ).add ( L"\\Coldboot VMs" );
		CoTaskMemFree ( documents );
		copy_text ( out, path.text ( ), MAX_PATH );

		return true;

	}

	bool folder_of ( const wchar_t* path, wchar_t* out ) {

		int32_t cut = -1;

		for ( int32_t i = 0; path [ i ]; i++ ) {

			if ( path [ i ] == L'\\' || path [ i ] == L'/' )
				cut = i;

		}

		if ( cut <= 0 )
			return false;

		copy_text ( out, path, cut + 1 );

		return true;

	}

	uint64_t running_hash ( const wchar_t* path ) {

		uint64_t hash = 0xCBF29CE484222325ull;

		for ( int32_t i = 0; path [ i ]; i++ ) {

			wchar_t c = path [ i ] >= L'A' && path [ i ] <= L'Z' ? static_cast < wchar_t > ( path [ i ] + 32 ) : ( path [ i ] == L'/' ? L'\\' : path [ i ] );

			hash = ( hash ^ static_cast < uint64_t > ( c ) ) * 0x100000001B3ull;

		}

		return hash | 1;

	}

	void running_name ( const wchar_t* path, wchar_t* out, int32_t room ) {

		base::writer name;

		name.add ( L"Local\\Coldboot.vm." ).hex ( running_hash ( path ), 16 );
		copy_text ( out, name.text ( ), room );

	}

	struct window_search {

		uint64_t hash;
		HWND found;

	};

	static BOOL __stdcall match_window ( HWND window, LPARAM context ) {

		window_search* search = reinterpret_cast < window_search* > ( context );

		if ( reinterpret_cast < uint64_t > ( GetPropW ( window, window_tag ) ) != search->hash )
			return TRUE;

		search->found = window;

		return FALSE;

	}

	HWND find_window ( const wchar_t* path ) {

		window_search search = { running_hash ( path ), nullptr };

		EnumWindows ( &match_window, reinterpret_cast < LPARAM > ( &search ) );

		return search.found;

	}

	bool is_running ( const wchar_t* path ) {

		wchar_t name [ 64 ];

		running_name ( path, name, 64 );

		HANDLE mutex = OpenMutexW ( SYNCHRONIZE, FALSE, name );

		if ( !mutex )
			return false;

		CloseHandle ( mutex );

		return true;

	}

	bool write_preview ( const wchar_t* folder, const uint32_t* pixels, int32_t width, int32_t height ) {

		if ( !pixels || width <= 0 || height <= 0 || width > 1024 || height > 512 )
			return false;

		base::list < char > bytes;
		base::writer path;
		uint32_t size [ 2 ] = { static_cast < uint32_t > ( width ), static_cast < uint32_t > ( height ) };

		bytes.reserve ( 12 + static_cast < size_t > ( width ) * height * 4 );
		bytes.push ( 'C' );
		bytes.push ( 'B' );
		bytes.push ( 'P' );
		bytes.push ( 'V' );

		for ( uint32_t value : size ) {

			for ( int32_t b = 0; b < 4; b++ )
				bytes.push ( static_cast < char > ( value >> ( b * 8 ) ) );

		}

		for ( int32_t i = 0; i < width * height; i++ ) {

			for ( int32_t b = 0; b < 4; b++ )
				bytes.push ( static_cast < char > ( pixels [ i ] >> ( b * 8 ) ) );

		}

		path.add ( folder ).add ( L"\\preview.bin" );

		return write_atomic ( path.text ( ), bytes.data ( ), bytes.count ( ) );

	}

	bool read_preview ( const wchar_t* folder, base::list < uint32_t >& pixels, int32_t& width, int32_t& height ) {

		base::list < char > data;
		base::writer path;

		path.add ( folder ).add ( L"\\preview.bin" );
		width = 0;
		height = 0;

		if ( !files::read ( path.text ( ), data ) || data.count ( ) - 1 < 12 )
			return false;

		const uint8_t* bytes = reinterpret_cast < const uint8_t* > ( data.data ( ) );
		uint32_t across = static_cast < uint32_t > ( bytes [ 4 ] | bytes [ 5 ] << 8 | bytes [ 6 ] << 16 | bytes [ 7 ] << 24 );
		uint32_t down = static_cast < uint32_t > ( bytes [ 8 ] | bytes [ 9 ] << 8 | bytes [ 10 ] << 16 | bytes [ 11 ] << 24 );

		if ( bytes [ 0 ] != 'C' || bytes [ 1 ] != 'B' || bytes [ 2 ] != 'P' || bytes [ 3 ] != 'V' || !across || !down || across > 1024 || down > 512 || static_cast < uint32_t > ( data.count ( ) - 1 ) != 12 + across * down * 4 )
			return false;

		pixels.fill ( static_cast < size_t > ( across ) * down, 0 );

		for ( uint32_t i = 0; i < across * down; i++ )
			pixels [ i ] = static_cast < uint32_t > ( bytes [ 12 + i * 4 ] | bytes [ 13 + i * 4 ] << 8 | bytes [ 14 + i * 4 ] << 16 );

		width = static_cast < int32_t > ( across );
		height = static_cast < int32_t > ( down );

		return true;

	}

	static bool erase_file ( const wchar_t* path ) {

		SetFileAttributesW ( path, FILE_ATTRIBUTE_NORMAL );

		if ( DeleteFileW ( path ) )
			return true;

		DWORD reason = GetLastError ( );

		return reason == ERROR_FILE_NOT_FOUND || reason == ERROR_PATH_NOT_FOUND;

	}

	static const wchar_t* leaf_of ( const wchar_t* path ) {

		const wchar_t* leaf = path;

		for ( const wchar_t* at = path; *at; at++ ) {

			if ( *at == L'\\' || *at == L'/' )
				leaf = at + 1;

		}

		return leaf;

	}

	static bool made_for ( const wchar_t* folder, const wchar_t* path ) {

		static constexpr int32_t suffix = 9;

		const wchar_t* leaf = leaf_of ( folder );
		const wchar_t* file = leaf_of ( path );
		int32_t length = base::text_length ( file ) - suffix;

		if ( length <= 0 || !same_path ( file + length, L".coldboot" ) || base::text_length ( leaf ) < length || CompareStringOrdinal ( leaf, length, file, length, TRUE ) != CSTR_EQUAL )
			return false;

		if ( !leaf [ length ] )
			return true;

		if ( leaf [ length ] != L' ' || !leaf [ length + 1 ] )
			return false;

		for ( int32_t i = length + 1; leaf [ i ]; i++ ) {

			if ( leaf [ i ] < L'0' || leaf [ i ] > L'9' )
				return false;

		}

		return true;

	}

	bool erase ( const wchar_t* path, const config* settings ) {

		wchar_t folder [ MAX_PATH ];
		wchar_t owner [ MAX_PATH ];

		if ( !folder_of ( path, folder ) )
			return false;

		if ( settings && settings->disk [ 0 ] && folder_of ( settings->disk, owner ) && same_path ( owner, folder ) && !erase_file ( settings->disk ) )
			return false;

		static const wchar_t* const leftovers [ ] = { L"\\preview.bin", L"\\preview.bin.new", L"\\nvram.bin", L"\\setup-helper-2.iso", L"\\sent-files-1.iso", L"\\sent-files-2.iso", L"\\coldboot-tools.iso", L"\\setup-answers.iso" };

		for ( const wchar_t* name : leftovers ) {

			base::writer extra;

			extra.add ( folder ).add ( name );
			erase_file ( extra.text ( ) );

		}

		base::writer staging;

		staging.add ( path ).add ( L".new" );
		erase_file ( staging.text ( ) );

		if ( !erase_file ( path ) )
			return false;

		if ( made_for ( folder, path ) )
			RemoveDirectoryW ( folder );

		return true;

	}

}