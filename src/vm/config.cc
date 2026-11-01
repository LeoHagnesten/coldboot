#include "vm/config.hh"
#include "base/files.hh"

namespace vm {

	static const preset preset_table [ ] = {

		{ "pcxt", L"IBM PC/XT", L"An empty XT with an 8088 at 4.77 MHz, CGA and two floppy drives", L"XT", 640 },
		{ "freedos", L"FreeDOS", L"An XT set up for the FreeDOS 1.3 floppy edition", L"FD", 640 },

	};

	const preset* presets ( int32_t& count ) {

		count = static_cast < int32_t > ( sizeof ( preset_table ) / sizeof ( preset_table [ 0 ] ) );

		return preset_table;

	}

	const preset* find_preset ( const char* id ) {

		for ( const preset& item : preset_table ) {

			if ( base::same_text ( item.id, id ) )
				return &item;

		}

		return nullptr;

	}

	static bool fail ( char* out, int32_t room, int32_t line, const char* text ) {

		if ( !out || room <= 0 )
			return false;

		int32_t used = 0;

		auto add = [ & ] ( const char* part ) {

			for ( int32_t i = 0; part [ i ] && used < room - 1; i++ )
				out [ used++ ] = part [ i ];

		};

		if ( line > 0 ) {

			char digits [ 12 ];
			int32_t count = 0;

			add ( "line " );

			do {

				digits [ count++ ] = static_cast < char > ( '0' + line % 10 );
				line /= 10;

			} while ( line );

			while ( count && used < room - 1 )
				out [ used++ ] = digits [ --count ];

			add ( ": " );

		}

		add ( text );
		out [ used ] = 0;

		return false;

	}

	static bool same_key ( const char* line, int32_t size, const char* key ) {

		int32_t length = base::text_length ( key );

		if ( length != size )
			return false;

		for ( int32_t i = 0; i < size; i++ ) {

			if ( line [ i ] != key [ i ] )
				return false;

		}

		return true;

	}

	static bool number ( const char* text, int32_t size, int64_t& out ) {

		bool negative = size > 0 && text [ 0 ] == '-';
		int32_t at = negative ? 1 : 0;

		if ( at >= size || size - at > 10 )
			return false;

		int64_t value = 0;

		for ( ; at < size; at++ ) {

			if ( text [ at ] < '0' || text [ at ] > '9' )
				return false;

			value = value * 10 + ( text [ at ] - '0' );

		}

		out = negative ? -value : value;

		return true;

	}

	static bool wide ( const char* text, int32_t size, wchar_t* out, int32_t room ) {

		if ( !size ) {

			out [ 0 ] = 0;

			return true;

		}

		int32_t written = MultiByteToWideChar ( CP_UTF8, MB_ERR_INVALID_CHARS, text, size, out, room - 1 );

		if ( written <= 0 )
			return false;

		out [ written ] = 0;

		return true;

	}

	static bool reserved ( const wchar_t* name ) {

		static const wchar_t* const devices [ ] = { L"CON", L"PRN", L"AUX", L"NUL" };
		wchar_t upper [ 8 ] = { };
		int32_t length = 0;

		for ( ; name [ length ] && name [ length ] != L'.' && length < 7; length++ )
			upper [ length ] = name [ length ] >= L'a' && name [ length ] <= L'z' ? static_cast < wchar_t > ( name [ length ] - 32 ) : name [ length ];

		if ( name [ length ] && name [ length ] != L'.' )
			return false;

		for ( const wchar_t* device : devices ) {

			if ( base::same_text ( upper, device ) )
				return true;

		}

		return length == 4 && ( ( upper [ 0 ] == L'C' && upper [ 1 ] == L'O' && upper [ 2 ] == L'M' ) || ( upper [ 0 ] == L'L' && upper [ 1 ] == L'P' && upper [ 2 ] == L'T' ) ) && upper [ 3 ] >= L'1' && upper [ 3 ] <= L'9';

	}

	bool valid_name ( const wchar_t* name ) {

		int32_t length = base::text_length ( name );
		bool visible = false;

		if ( length < 1 || length >= name_room || name [ length - 1 ] == L' ' || name [ length - 1 ] == L'.' )
			return false;

		for ( int32_t i = 0; i < length; i++ ) {

			wchar_t c = name [ i ];

			if ( c < 32 || c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|' )
				return false;

			visible = visible || c != L' ';

		}

		return visible && !reserved ( name );

	}

	bool parse ( const char* text, int32_t length, config& out, char* error, int32_t room ) {

		config* result = new config ( );
		bool versioned = false;
		bool named = false;
		int32_t line_number = 0;
		int32_t start = 0;
		bool ok = true;

		if ( length >= 3 && static_cast < uint8_t > ( text [ 0 ] ) == 0xEF && static_cast < uint8_t > ( text [ 1 ] ) == 0xBB && static_cast < uint8_t > ( text [ 2 ] ) == 0xBF )
			start = 3;

		while ( ok && start < length ) {

			int32_t end = start;

			while ( end < length && text [ end ] != '\n' )
				end++;

			int32_t size = end - start;
			const char* line = text + start;

			line_number++;
			start = end + 1;

			if ( size && line [ size - 1 ] == '\r' )
				size--;

			if ( !size )
				continue;

			int32_t equals = 0;

			while ( equals < size && line [ equals ] != '=' )
				equals++;

			if ( !equals || equals == size ) {

				ok = fail ( error, room, line_number, "expected key=value" );

				break;

			}

			const char* value = line + equals + 1;
			int32_t value_size = size - equals - 1;
			int64_t amount = 0;

			auto key = [ & ] ( const char* name ) {

				return same_key ( line, equals, name );

			};

			auto flag = [ & ] ( bool& target ) {

				if ( value_size != 1 || ( value [ 0 ] != '0' && value [ 0 ] != '1' ) )
					return fail ( error, room, line_number, "expected 0 or 1" );

				target = value [ 0 ] == '1';

				return true;

			};

			auto path = [ & ] ( wchar_t* target ) {

				return wide ( value, value_size, target, MAX_PATH ) || fail ( error, room, line_number, "the path is too long or isn't valid UTF-8" );

			};

			if ( !versioned ) {

				if ( !key ( "coldboot" ) )
					ok = fail ( error, room, line_number, "this isn't a Coldboot machine file" );
				else if ( !number ( value, value_size, amount ) || amount < 1 )
					ok = fail ( error, room, line_number, "the version line is broken" );
				else if ( amount > config_version )
					ok = fail ( error, room, line_number, "this machine was made by a newer Coldboot" );

				versioned = true;

				continue;

			}

			if ( key ( "name" ) ) {

				if ( !wide ( value, value_size, result->name, name_room ) || !valid_name ( result->name ) )
					ok = fail ( error, room, line_number, "the name is empty or has characters Windows can't use in a folder name" );

				named = true;

			} else if ( key ( "os" ) ) {

				bool clean = value_size > 0 && value_size < static_cast < int32_t > ( sizeof ( result->os ) );

				for ( int32_t i = 0; clean && i < value_size; i++ )
					clean = ( value [ i ] >= 'a' && value [ i ] <= 'z' ) || ( value [ i ] >= '0' && value [ i ] <= '9' ) || value [ i ] == '-';

				if ( !clean ) {

					ok = fail ( error, room, line_number, "the OS id isn't valid" );

				} else {

					for ( int32_t i = 0; i < value_size; i++ )
						result->os [ i ] = value [ i ];

					result->os [ value_size ] = 0;

				}

			} else if ( key ( "machine" ) ) {

				if ( !same_key ( value, value_size, "xt" ) )
					ok = fail ( error, room, line_number, "Coldboot can't run this machine type yet" );

			} else if ( key ( "memory" ) ) {

				if ( !number ( value, value_size, amount ) || amount < 64 || amount > 640 || amount % 16 )
					ok = fail ( error, room, line_number, "memory has to be 64 to 640 KB in steps of 16" );
				else
					result->memory = static_cast < uint32_t > ( amount );

			} else if ( key ( "turbo" ) )
				ok = flag ( result->turbo );
			else if ( key ( "crt" ) )
				ok = flag ( result->crt );
			else if ( key ( "fast_floppy" ) )
				ok = flag ( result->fast_floppy );
			else if ( key ( "custom_bios" ) )
				ok = flag ( result->custom_bios );
			else if ( key ( "maximized" ) )
				ok = flag ( result->maximized );
			else if ( key ( "window_x" ) || key ( "window_y" ) || key ( "window_width" ) || key ( "window_height" ) ) {

				if ( !number ( value, value_size, amount ) || amount < -100000 || amount > 100000 ) {

					ok = fail ( error, room, line_number, "the window position isn't a number" );

				} else {

					int32_t place = static_cast < int32_t > ( amount );

					if ( key ( "window_x" ) )
						result->window_x = place;
					else if ( key ( "window_y" ) )
						result->window_y = place;
					else if ( key ( "window_width" ) )
						result->window_width = place;
					else
						result->window_height = place;

				}

			} else if ( key ( "bios" ) )
				ok = path ( result->bios );
			else if ( key ( "floppy_a" ) )
				ok = path ( result->floppies [ 0 ] );
			else if ( key ( "floppy_b" ) )
				ok = path ( result->floppies [ 1 ] );
			else if ( key ( "notes" ) ) {

				wchar_t* raw = new wchar_t [ notes_room ];

				if ( !wide ( value, value_size, raw, notes_room ) ) {

					ok = fail ( error, room, line_number, "the notes are too long or aren't valid UTF-8" );

				} else {

					int32_t used = 0;

					for ( int32_t i = 0; raw [ i ] && used < notes_room - 1; i++ ) {

						wchar_t c = raw [ i ];

						if ( c == L'\\' && raw [ i + 1 ] ) {

							i++;
							c = raw [ i ] == L'n' ? L'\n' : raw [ i ];

						}

						result->notes [ used++ ] = c;

					}

					result->notes [ used ] = 0;

				}

				delete [ ] raw;

			}

		}

		if ( ok && !versioned )
			ok = fail ( error, room, 0, "the file is empty" );

		if ( ok && !named )
			ok = fail ( error, room, 0, "the machine has no name" );

		if ( ok )
			out = *result;

		delete result;

		return ok;

	}

	static void put ( base::list < char >& out, const char* text ) {

		for ( int32_t i = 0; text [ i ]; i++ )
			out.push ( text [ i ] );

	}

	static void put_number ( base::list < char >& out, const char* key, int64_t value ) {

		char digits [ 24 ];
		int32_t count = 0;
		bool negative = value < 0;
		uint64_t magnitude = negative ? static_cast < uint64_t > ( -value ) : static_cast < uint64_t > ( value );

		do {

			digits [ count++ ] = static_cast < char > ( '0' + magnitude % 10 );
			magnitude /= 10;

		} while ( magnitude );

		put ( out, key );
		out.push ( '=' );

		if ( negative )
			out.push ( '-' );

		while ( count )
			out.push ( digits [ --count ] );

		out.push ( '\n' );

	}

	static void put_text ( base::list < char >& out, const char* key, const wchar_t* text, bool escape ) {

		put ( out, key );
		out.push ( '=' );

		for ( int32_t i = 0; text [ i ]; i++ ) {

			wchar_t c = text [ i ];
			char bytes [ 8 ];

			if ( escape && ( c == L'\\' || c == L'\n' ) ) {

				out.push ( '\\' );
				out.push ( c == L'\n' ? 'n' : '\\' );

				continue;

			}

			if ( c == L'\r' || ( c == L'\n' && !escape ) )
				continue;

			int32_t pair = c >= 0xD800 && c <= 0xDBFF && text [ i + 1 ] ? 2 : 1;
			int32_t size = WideCharToMultiByte ( CP_UTF8, 0, text + i, pair, bytes, sizeof ( bytes ), nullptr, nullptr );

			for ( int32_t b = 0; b < size; b++ )
				out.push ( bytes [ b ] );

			i += pair - 1;

		}

		out.push ( '\n' );

	}

	void serialize ( const config& in, base::list < char >& out ) {

		put_number ( out, "coldboot", config_version );
		put_text ( out, "name", in.name, false );
		put ( out, "os=" );
		put ( out, in.os );
		put ( out, "\nmachine=xt\n" );
		put_number ( out, "memory", in.memory );
		put_number ( out, "turbo", in.turbo );
		put_number ( out, "crt", in.crt );
		put_number ( out, "fast_floppy", in.fast_floppy );
		put_number ( out, "custom_bios", in.custom_bios );
		put_text ( out, "bios", in.bios, false );
		put_text ( out, "floppy_a", in.floppies [ 0 ], false );
		put_text ( out, "floppy_b", in.floppies [ 1 ], false );
		put_number ( out, "maximized", in.maximized );
		put_number ( out, "window_x", in.window_x );
		put_number ( out, "window_y", in.window_y );
		put_number ( out, "window_width", in.window_width );
		put_number ( out, "window_height", in.window_height );
		put_text ( out, "notes", in.notes, true );

	}

	bool load ( const wchar_t* path, config& out, char* error, int32_t room ) {

		base::list < char > data;

		if ( !files::read ( path, data ) )
			return fail ( error, room, 0, "the file can't be read" );

		return parse ( data.data ( ), data.count ( ) - 1, out, error, room );

	}

	bool save ( const wchar_t* path, const config& in ) {

		base::list < char > text;
		base::writer staging;

		serialize ( in, text );
		staging.add ( path ).add ( L".new" );

		if ( !files::write ( staging.text ( ), text.data ( ), text.count ( ) ) )
			return false;

		return MoveFileExW ( staging.text ( ), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) != 0;

	}

}