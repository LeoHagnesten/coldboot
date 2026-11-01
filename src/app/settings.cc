#include "app/settings.hh"
#include "base/files.hh"

namespace app {

	const wchar_t* data_path ( const wchar_t* name ) {

		static base::writer path;

		path.clear ( );
		path.add ( files::folder ( ) ).add ( L"\\" ).add ( name );

		return path.text ( );

	}

	static bool key_is ( const char* line, int32_t length, const char* key ) {

		int32_t size = base::text_length ( key );

		if ( length <= size || line [ size ] != '=' )
			return false;

		for ( int32_t i = 0; i < size; i++ ) {

			if ( line [ i ] != key [ i ] )
				return false;

		}

		return true;

	}

	static int32_t number ( const char* text, int32_t length ) {

		int32_t value = 0;
		bool negative = false;
		int32_t i = 0;

		if ( i < length && text [ i ] == '-' ) {

			negative = true;
			i++;

		}

		for ( ; i < length && text [ i ] >= '0' && text [ i ] <= '9'; i++ )
			value = value * 10 + ( text [ i ] - '0' );

		return negative ? -value : value;

	}

	static void path_value ( const char* text, int32_t length, wchar_t* out ) {

		int32_t written = length > 0 ? MultiByteToWideChar ( CP_UTF8, 0, text, length, out, MAX_PATH - 1 ) : 0;

		out [ written > 0 ? written : 0 ] = 0;

	}

	void load_settings ( settings& out ) {

		base::list < char > data;

		if ( !files::read ( data_path ( L"settings.ini" ), data ) )
			return;

		const char* text = data.data ( );
		int32_t total = data.count ( ) - 1;
		int32_t start = 0;

		while ( start < total ) {

			int32_t end = start;

			while ( end < total && text [ end ] != '\n' )
				end++;

			int32_t length = end - start;

			if ( length && text [ end - 1 ] == '\r' )
				length--;

			const char* line = text + start;
			const char* equals = line;

			while ( equals < line + length && *equals != '=' )
				equals++;

			const char* value = equals + 1;
			int32_t size = static_cast < int32_t > ( line + length - value );

			if ( equals < line + length ) {

				if ( key_is ( line, length, "theme" ) )
					out.theme = static_cast < theme_choice > ( base::clamp ( number ( value, size ), 0, 2 ) );
				else if ( key_is ( line, length, "light" ) ) {

					if ( number ( value, size ) != 0 )
						out.theme = THEME_LIGHT;

				} else if ( key_is ( line, length, "turbo" ) )
					out.turbo = number ( value, size ) != 0;
				else if ( key_is ( line, length, "crt" ) )
					out.crt = number ( value, size ) != 0;
				else if ( key_is ( line, length, "fast_floppy" ) )
					out.fast_floppy = number ( value, size ) != 0;
				else if ( key_is ( line, length, "custom_bios" ) )
					out.custom_bios = number ( value, size ) != 0;
				else if ( key_is ( line, length, "maximized" ) )
					out.maximized = number ( value, size ) != 0;
				else if ( key_is ( line, length, "memory" ) )
					out.memory = static_cast < uint32_t > ( base::clamp ( number ( value, size ), 64, 640 ) );
				else if ( key_is ( line, length, "window_x" ) )
					out.window_x = number ( value, size );
				else if ( key_is ( line, length, "window_y" ) )
					out.window_y = number ( value, size );
				else if ( key_is ( line, length, "window_width" ) )
					out.window_width = number ( value, size );
				else if ( key_is ( line, length, "window_height" ) )
					out.window_height = number ( value, size );
				else if ( key_is ( line, length, "bios" ) )
					path_value ( value, size, out.bios );
				else if ( key_is ( line, length, "disk_a" ) )
					path_value ( value, size, out.disks [ 0 ] );
				else if ( key_is ( line, length, "disk_b" ) )
					path_value ( value, size, out.disks [ 1 ] );

			}

			start = end + 1;

		}

	}

	static void put_text ( base::list < char >& out, const char* text ) {

		for ( int32_t i = 0; text [ i ]; i++ )
			out.push ( text [ i ] );

	}

	static void put_number ( base::list < char >& out, const char* key, int32_t value ) {

		char digits [ 16 ];
		int32_t count = 0;
		bool negative = value < 0;
		uint32_t magnitude = negative ? static_cast < uint32_t > ( -value ) : static_cast < uint32_t > ( value );

		do {

			digits [ count++ ] = static_cast < char > ( '0' + magnitude % 10 );
			magnitude /= 10;

		} while ( magnitude );

		put_text ( out, key );
		out.push ( '=' );

		if ( negative )
			out.push ( '-' );

		while ( count )
			out.push ( digits [ --count ] );

		out.push ( '\n' );

	}

	static void put_path ( base::list < char >& out, const char* key, const wchar_t* path ) {

		char text [ MAX_PATH * 3 ];
		int32_t size = path [ 0 ] ? WideCharToMultiByte ( CP_UTF8, 0, path, -1, text, sizeof ( text ), nullptr, nullptr ) : 0;

		put_text ( out, key );
		out.push ( '=' );

		for ( int32_t i = 0; i < size - 1; i++ )
			out.push ( text [ i ] );

		out.push ( '\n' );

	}

	void save_settings ( const settings& in ) {

		base::list < char > out;

		put_number ( out, "theme", in.theme );
		put_number ( out, "turbo", in.turbo );
		put_number ( out, "crt", in.crt );
		put_number ( out, "fast_floppy", in.fast_floppy );
		put_number ( out, "custom_bios", in.custom_bios );
		put_number ( out, "maximized", in.maximized );
		put_number ( out, "memory", static_cast < int32_t > ( in.memory ) );
		put_number ( out, "window_x", in.window_x );
		put_number ( out, "window_y", in.window_y );
		put_number ( out, "window_width", in.window_width );
		put_number ( out, "window_height", in.window_height );
		put_path ( out, "bios", in.bios );
		put_path ( out, "disk_a", in.disks [ 0 ] );
		put_path ( out, "disk_b", in.disks [ 1 ] );
		files::write_later ( data_path ( L"settings.ini" ), out.data ( ), out.count ( ) );

	}

	bool system_light ( ) {

		DWORD value = 0;
		DWORD size = sizeof ( value );

		return RegGetValueW ( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size ) == ERROR_SUCCESS && value != 0;

	}

	bool use_light ( const settings& in ) {

		return in.theme == THEME_LIGHT || ( in.theme == THEME_SYSTEM && system_light ( ) );

	}

}