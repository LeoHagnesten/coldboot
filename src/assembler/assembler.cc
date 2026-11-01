#include "assembler/internal.hh"

namespace assembler {

	static bool name_start ( char c ) {

		return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '_' || c == '.' || c == '$' || c == '@' || c == '?';

	}

	static bool name_part ( char c ) {

		return name_start ( c ) || ( c >= '0' && c <= '9' );

	}

	static int32_t digit_value ( char c ) {

		if ( c >= '0' && c <= '9' )
			return c - '0';

		if ( c >= 'a' && c <= 'f' )
			return c - 'a' + 10;

		if ( c >= 'A' && c <= 'F' )
			return c - 'A' + 10;

		return -1;

	}

	static char lower ( char c ) {

		return c >= 'A' && c <= 'Z' ? static_cast < char > ( c - 'A' + 'a' ) : c;

	}

	static bool parse_number ( const char* text, int32_t length, int64_t& value ) {

		int32_t base = 10;
		int32_t first = 0;
		int32_t last = length;

		if ( length > 2 && text [ 0 ] == '0' && lower ( text [ 1 ] ) == 'x' ) {

			base = 16;
			first = 2;

		} else if ( length > 1 && lower ( text [ length - 1 ] ) == 'h' ) {

			base = 16;
			last = length - 1;

		} else if ( length > 2 && text [ 0 ] == '0' && lower ( text [ 1 ] ) == 'b' ) {

			base = 2;
			first = 2;

		}

		if ( first >= last )
			return false;

		value = 0;

		for ( int32_t i = first; i < last; i++ ) {

			if ( text [ i ] == '_' )
				continue;

			int32_t digit = digit_value ( text [ i ] );

			if ( digit < 0 || digit >= base )
				return false;

			value = value * base + digit;

		}

		return true;

	}

	static void copy_text ( char* out, int32_t room, const char* text, int32_t length ) {

		int32_t used = base::text_length ( out );

		for ( int32_t i = 0; i < length && used < room - 1; i++ )
			out [ used++ ] = text [ i ];

		out [ used ] = 0;

	}

	static int32_t operator_level ( const token& item ) {

		if ( item.kind != TOKEN_MARK )
			return -1;

		switch ( item.mark ) {

			case '|':
				return 0;

			case '^':
				return 1;

			case '&':
				return 2;

			case '<':
			case '>':
				return item.length == 2 ? 3 : -1;

			case '+':
			case '-':
				return 4;

			case '*':
			case '/':
			case '%':
				return 5;

			default:
				return -1;

		}

	}

	bool program::fail ( const char* message ) {

		if ( output && !output->error [ 0 ] ) {

			copy_text ( output->error, sizeof ( output->error ), message, base::text_length ( message ) );
			output->error_line = current ? current->line : failing_line;

		}

		return false;

	}

	bool program::fail ( const char* message, const token& detail ) {

		if ( output && !output->error [ 0 ] ) {

			copy_text ( output->error, sizeof ( output->error ), message, base::text_length ( message ) );
			copy_text ( output->error, sizeof ( output->error ), ": ", 2 );
			copy_text ( output->error, sizeof ( output->error ), detail.start, detail.length );
			output->error_line = current ? current->line : failing_line;

		}

		return false;

	}

	bool program::same ( const token& item, const char* text ) const {

		if ( item.kind != TOKEN_NAME )
			return false;

		for ( int32_t i = 0; i < item.length; i++ ) {

			if ( !text [ i ] || lower ( item.start [ i ] ) != text [ i ] )
				return false;

		}

		return text [ item.length ] == 0;

	}

	bool program::small ( statement& item, bool fits ) {

		if ( !item.wide && fits )
			return true;

		item.wide = true;

		return false;

	}

	bool program::lex ( const char* text, int32_t length ) {

		int32_t i = 0;
		int32_t line = 1;

		while ( i < length ) {

			char c = text [ i ];

			if ( c == '\n' ) {

				tokens.emplace ( );
				line++;
				i++;

				continue;

			}

			if ( c == ' ' || c == '\t' || c == '\r' ) {

				i++;

				continue;

			}

			if ( c == ';' ) {

				while ( i < length && text [ i ] != '\n' )
					i++;

				continue;

			}

			failing_line = line;

			token item;

			item.start = text + i;

			if ( c == '"' || c == '\'' ) {

				int32_t j = i + 1;

				while ( j < length && text [ j ] != c && text [ j ] != '\n' )
					j++;

				if ( j >= length || text [ j ] != c )
					return fail ( "string has no closing quote" );

				item.kind = TOKEN_STRING;
				item.start = text + i + 1;
				item.length = j - i - 1;
				i = j + 1;

			} else if ( c >= '0' && c <= '9' ) {

				int32_t j = i;

				while ( j < length && name_part ( text [ j ] ) )
					j++;

				item.kind = TOKEN_NUMBER;
				item.length = j - i;

				if ( !parse_number ( item.start, item.length, item.value ) )
					return fail ( "bad number", item );

				i = j;

			} else if ( name_start ( c ) ) {

				int32_t j = i;

				while ( j < length && name_part ( text [ j ] ) )
					j++;

				item.kind = TOKEN_NAME;
				item.length = j - i;
				i = j;

			} else {

				const char* marks = ",[]:+-*/%&|^~()<>";
				bool allowed = false;

				for ( int32_t m = 0; marks [ m ]; m++ )
					allowed = allowed || marks [ m ] == c;

				item.kind = TOKEN_MARK;
				item.mark = c;
				item.length = 1;

				if ( !allowed )
					return fail ( "unexpected character", item );

				if ( ( c == '<' || c == '>' ) && i + 1 < length && text [ i + 1 ] == c )
					item.length = 2;

				i += item.length;

			}

			tokens.push ( item );

		}

		tokens.emplace ( );

		return true;

	}

	uint32_t program::hash ( const char* text, int32_t length ) const {

		uint32_t value = 2166136261u;

		for ( int32_t i = 0; i < length; i++ )
			value = ( value ^ static_cast < uint8_t > ( text [ i ] ) ) * 16777619u;

		return value;

	}

	int32_t program::find_name ( const char* text, int32_t length ) const {

		uint32_t slot = hash ( text, length ) & ( table_size - 1 );

		while ( table [ slot ] >= 0 ) {

			const definition& entry = definitions [ table [ slot ] ];
			bool equal = entry.length == length;

			for ( int32_t i = 0; equal && i < length; i++ )
				equal = names [ entry.name + i ] == text [ i ];

			if ( equal )
				return table [ slot ];

			slot = ( slot + 1 ) & ( table_size - 1 );

		}

		return -1;

	}

	static int32_t full_name ( const base::list < char >& names, const base::list < definition >& definitions, const token& item, int32_t scope, char* out, int32_t room ) {

		int32_t length = 0;

		if ( item.length && item.start [ 0 ] == '.' && scope >= 0 ) {

			const definition& owner = definitions [ scope ];

			for ( int32_t i = 0; i < owner.length && length < room; i++ )
				out [ length++ ] = names [ owner.name + i ];

		}

		for ( int32_t i = 0; i < item.length && length < room; i++ )
			out [ length++ ] = item.start [ i ];

		return length;

	}

	int32_t program::resolve ( const token& item, int32_t scope ) const {

		char text [ 160 ];
		int32_t length = full_name ( names, definitions, item, scope, text, 160 );

		return find_name ( text, length );

	}

	bool program::define ( const token& item, int32_t scope, const definition& entry ) {

		char text [ 160 ];
		int32_t length = full_name ( names, definitions, item, scope, text, 160 );

		if ( find_name ( text, length ) >= 0 )
			return fail ( "defined twice", item );

		definition stored = entry;

		stored.name = names.count ( );
		stored.length = length;

		for ( int32_t i = 0; i < length; i++ )
			names.push ( text [ i ] );

		names.push ( 0 );

		uint32_t slot = hash ( text, length ) & ( table_size - 1 );

		while ( table [ slot ] >= 0 )
			slot = ( slot + 1 ) & ( table_size - 1 );

		table [ slot ] = definitions.count ( );
		definitions.push ( stored );

		return true;

	}

	bool program::collect ( ) {

		int32_t line = 1;
		int32_t scope = -1;
		int32_t at = 0;
		int32_t total = tokens.count ( );

		while ( at < total ) {

			int32_t start = at;

			while ( tokens [ at ].kind != TOKEN_END )
				at++;

			int32_t end = at++;

			failing_line = line;

			if ( end - start >= 2 && tokens [ start ].kind == TOKEN_NAME && tokens [ start + 1 ].kind == TOKEN_MARK && tokens [ start + 1 ].mark == ':' ) {

				definition entry;

				entry.statement = statements.count ( );
				entry.scope = scope;

				if ( !define ( tokens [ start ], scope, entry ) )
					return false;

				if ( tokens [ start ].start [ 0 ] != '.' )
					scope = definitions.count ( ) - 1;

				start += 2;

			} else if ( end - start >= 3 && tokens [ start ].kind == TOKEN_NAME && same ( tokens [ start + 1 ], "equ" ) ) {

				definition entry;

				entry.first = start + 2;
				entry.count = end - start - 2;
				entry.scope = scope;

				if ( !define ( tokens [ start ], scope, entry ) )
					return false;

				line++;

				continue;

			}

			statement item;

			item.line = line;
			item.first = start;
			item.count = end - start;
			item.scope = scope;
			statements.push ( item );
			line++;

		}

		return true;

	}

	bool program::symbol_value ( int32_t index, int64_t& value, bool& known ) {

		definition& entry = definitions [ index ];

		if ( entry.statement >= 0 ) {

			const statement& target = statements [ entry.statement ];

			value = target.address;
			known = known && target.placed;

			return true;

		}

		if ( entry.resolving )
			return fail ( "symbol is defined in terms of itself" );

		int32_t saved = active_scope;
		int32_t at = entry.first;

		entry.resolving = true;
		active_scope = entry.scope;

		bool ok = expression ( at, entry.first + entry.count, 0, value, known );

		active_scope = saved;
		entry.resolving = false;

		if ( ok && at != entry.first + entry.count )
			return fail ( "unexpected text in equ", tokens [ at ] );

		return ok;

	}

	bool program::primary ( int32_t& at, int32_t end, int64_t& value, bool& known ) {

		if ( at >= end )
			return fail ( "expression expected" );

		const token& item = tokens [ at ];

		if ( item.kind == TOKEN_MARK ) {

			if ( item.mark == '(' ) {

				at++;

				if ( !expression ( at, end, 0, value, known ) )
					return false;

				if ( at >= end || tokens [ at ].kind != TOKEN_MARK || tokens [ at ].mark != ')' )
					return fail ( "missing )" );

				at++;

				return true;

			}

			if ( item.mark == '-' || item.mark == '~' || item.mark == '+' ) {

				at++;

				if ( !primary ( at, end, value, known ) )
					return false;

				if ( item.mark == '-' )
					value = -value;
				else if ( item.mark == '~' )
					value = ~value;

				return true;

			}

			return fail ( "unexpected mark in expression", item );

		}

		at++;

		if ( item.kind == TOKEN_NUMBER ) {

			value = item.value;

			return true;

		}

		if ( item.kind == TOKEN_STRING ) {

			if ( item.length < 1 || item.length > 2 )
				return fail ( "only one or two characters can be used as a number", item );

			value = static_cast < uint8_t > ( item.start [ 0 ] );

			if ( item.length == 2 )
				value |= static_cast < int64_t > ( static_cast < uint8_t > ( item.start [ 1 ] ) ) << 8;

			return true;

		}

		if ( item.length == 1 && item.start [ 0 ] == '$' ) {

			value = current ? current->address : 0;

			return true;

		}

		if ( item.length == 2 && item.start [ 0 ] == '$' && item.start [ 1 ] == '$' ) {

			value = origin;

			return true;

		}

		int32_t index = resolve ( item, active_scope );

		if ( index < 0 ) {

			if ( final )
				return fail ( "unknown symbol", item );

			value = 0;
			known = false;

			return true;

		}

		return symbol_value ( index, value, known );

	}

	bool program::expression ( int32_t& at, int32_t end, int32_t level, int64_t& value, bool& known ) {

		if ( level > 5 )
			return primary ( at, end, value, known );

		if ( !expression ( at, end, level + 1, value, known ) )
			return false;

		while ( at < end && operator_level ( tokens [ at ] ) == level ) {

			const token& op = tokens [ at++ ];
			int64_t right = 0;

			if ( !expression ( at, end, level + 1, right, known ) )
				return false;

			switch ( op.mark ) {

				case '|':
					value |= right;
					break;

				case '^':
					value ^= right;
					break;

				case '&':
					value &= right;
					break;

				case '<':
					value <<= right & 63;
					break;

				case '>':
					value >>= right & 63;
					break;

				case '+':
					value += right;
					break;

				case '-':
					value -= right;
					break;

				case '*':
					value *= right;
					break;

				default:

					if ( !right ) {

						if ( known )
							return fail ( "division by zero" );

						right = 1;

					}

					value = op.mark == '/' ? value / right : value % right;
					break;

			}

		}

		return true;

	}

	bool program::evaluate ( int32_t first, int32_t count, int64_t& value, bool& known ) {

		int32_t at = first;

		value = 0;

		if ( !count )
			return fail ( "value expected" );

		if ( !expression ( at, first + count, 0, value, known ) )
			return false;

		if ( at != first + count )
			return fail ( "unexpected text", tokens [ at ] );

		return true;

	}

	bool program::split ( int32_t first, int32_t count, int32_t* starts, int32_t* counts, int32_t& found, int32_t room ) {

		int32_t depth = 0;
		int32_t start = first;

		found = 0;

		for ( int32_t i = first; i <= first + count; i++ ) {

			bool last = i == first + count;
			const token& item = tokens [ i ];

			if ( !last && item.kind == TOKEN_MARK ) {

				if ( item.mark == '[' || item.mark == '(' )
					depth++;
				else if ( item.mark == ']' || item.mark == ')' )
					depth--;

			}

			if ( last || ( depth == 0 && item.kind == TOKEN_MARK && item.mark == ',' ) ) {

				if ( found == room )
					return fail ( "too many operands" );

				if ( i == start )
					return fail ( "empty operand" );

				starts [ found ] = start;
				counts [ found ] = i - start;
				found++;
				start = i + 1;

			}

		}

		return true;

	}

	bool program::data ( int32_t first, int32_t count, int32_t unit, int64_t repeat, base::list < uint8_t >* bytes, uint32_t& size ) {

		static int32_t starts [ 256 ];
		static int32_t counts [ 256 ];
		int32_t found = 0;
		uint8_t chunk [ 1024 ];
		int32_t used = 0;

		if ( !split ( first, count, starts, counts, found, 256 ) )
			return false;

		for ( int32_t i = 0; i < found; i++ ) {

			const token& item = tokens [ starts [ i ] ];

			if ( counts [ i ] == 1 && item.kind == TOKEN_STRING && unit == 1 ) {

				for ( int32_t c = 0; c < item.length && used < 1024; c++ )
					chunk [ used++ ] = static_cast < uint8_t > ( item.start [ c ] );

				continue;

			}

			int64_t value = 0;
			bool known = true;

			if ( !evaluate ( starts [ i ], counts [ i ], value, known ) )
				return false;

			int64_t low = unit == 1 ? -128 : ( unit == 2 ? -32768 : -2147483648ll );
			int64_t high = unit == 1 ? 255 : ( unit == 2 ? 65535 : 4294967295ll );

			if ( final && ( value < low || value > high ) )
				return fail ( "value doesn't fit", item );

			for ( int32_t b = 0; b < unit && used < 1024; b++ )
				chunk [ used++ ] = static_cast < uint8_t > ( value >> ( b * 8 ) );

		}

		if ( used >= 1024 )
			return fail ( "data line is too long" );

		size = static_cast < uint32_t > ( used * repeat );

		if ( bytes ) {

			for ( int64_t r = 0; r < repeat; r++ ) {

				for ( int32_t b = 0; b < used; b++ )
					bytes->push ( chunk [ b ] );

			}

		}

		return true;

	}

	bool program::place ( statement& item, uint32_t& here, bool& started, base::list < uint8_t >* bytes ) {

		current = &item;
		active_scope = item.scope;
		item.address = here;
		item.placed = true;

		uint32_t size = 0;
		int32_t end = item.first + item.count;

		if ( item.count ) {

			const token& head = tokens [ item.first ];
			int32_t unit = same ( head, "db" ) ? 1 : ( same ( head, "dw" ) ? 2 : ( same ( head, "dd" ) ? 4 : 0 ) );

			if ( same ( head, "org" ) ) {

				int64_t value = 0;
				bool known = true;

				if ( !evaluate ( item.first + 1, item.count - 1, value, known ) )
					return false;

				if ( !known )
					return fail ( "org needs a value that is already known" );

				if ( !started ) {

					origin = static_cast < uint32_t > ( value );
					here = origin;
					item.address = here;

				} else if ( static_cast < uint32_t > ( value ) >= here ) {

					size = static_cast < uint32_t > ( value ) - here;

					for ( uint32_t i = 0; bytes && i < size; i++ )
						bytes->push ( 0xFF );

				} else if ( final ) {

					char message [ 80 ] = "code runs past org by ";
					char digits [ 12 ];
					int32_t length = 0;
					uint32_t over = here - static_cast < uint32_t > ( value );

					do {

						digits [ length++ ] = static_cast < char > ( '0' + over % 10 );
						over /= 10;

					} while ( over );

					for ( int32_t i = length - 1; i >= 0; i-- )
						copy_text ( message, 80, &digits [ i ], 1 );

					copy_text ( message, 80, " bytes", 6 );

					return fail ( message );

				}

			} else if ( same ( head, "align" ) ) {

				int64_t value = 0;
				bool known = true;

				if ( !evaluate ( item.first + 1, item.count - 1, value, known ) )
					return false;

				if ( !known || value <= 0 )
					return fail ( "align needs a positive value" );

				size = static_cast < uint32_t > ( ( value - here % value ) % value );

				for ( uint32_t i = 0; bytes && i < size; i++ )
					bytes->push ( 0x90 );

			} else if ( unit ) {

				if ( !data ( item.first + 1, item.count - 1, unit, 1, bytes, size ) )
					return false;

			} else if ( same ( head, "times" ) ) {

				int32_t directive = -1;

				for ( int32_t i = item.first + 1; i < end && directive < 0; i++ ) {

					if ( same ( tokens [ i ], "db" ) || same ( tokens [ i ], "dw" ) )
						directive = i;

				}

				if ( directive < 0 )
					return fail ( "times works with db and dw" );

				int64_t repeat = 0;
				bool known = true;

				if ( !evaluate ( item.first + 1, directive - item.first - 1, repeat, known ) )
					return false;

				if ( !known )
					return fail ( "times needs a count that is already known" );

				if ( repeat < 0 ) {

					if ( final )
						return fail ( "times count is negative" );

					repeat = 0;

				}

				if ( !data ( directive + 1, end - directive - 1, same ( tokens [ directive ], "db" ) ? 1 : 2, repeat, bytes, size ) )
					return false;

			} else if ( same ( head, "incbin" ) ) {

				if ( item.count != 2 || tokens [ item.first + 1 ].kind != TOKEN_STRING )
					return fail ( "incbin needs a name in quotes" );

				const token& name = tokens [ item.first + 1 ];
				const blob* found = nullptr;

				for ( int32_t i = 0; i < blob_count && !found; i++ ) {

					int32_t length = base::text_length ( blobs [ i ].name );
					bool equal = length == name.length;

					for ( int32_t c = 0; equal && c < length; c++ )
						equal = blobs [ i ].name [ c ] == name.start [ c ];

					if ( equal )
						found = &blobs [ i ];

				}

				if ( !found )
					return fail ( "nothing to include under that name", name );

				size = found->size;

				for ( uint32_t i = 0; bytes && i < size; i++ )
					bytes->push ( found->data [ i ] );

			} else {

				emitter out;

				if ( !encode ( item, item.first, item.count, out ) )
					return false;

				size = static_cast < uint32_t > ( out.size );

				for ( int32_t i = 0; bytes && i < out.size; i++ )
					bytes->push ( out.bytes [ i ] );

			}

		}

		if ( size )
			started = true;

		item.size = size;
		here += size;

		return true;

	}

	bool program::layout ( bool& changed ) {

		uint32_t here = 0;
		bool started = false;
		base::list < uint8_t >* bytes = final ? &output->bytes : nullptr;

		origin = 0;
		changed = false;

		for ( statement& item : statements ) {

			uint32_t before = item.size;

			if ( !place ( item, here, started, bytes ) )
				return false;

			if ( item.size != before )
				changed = true;

		}

		current = nullptr;

		return true;

	}

	bool program::run ( const char* text, int32_t length, const blob* blob_list, int32_t blob_total, result& out ) {

		output = &out;
		blobs = blob_list;
		blob_count = blob_total;
		out.bytes.clear ( );
		out.symbols.clear ( );
		out.lines.clear ( );
		out.error [ 0 ] = 0;
		out.error_line = 0;
		table.fill ( table_size, -1 );

		if ( !lex ( text, length ) || !collect ( ) )
			return false;

		bool settled = false;

		for ( int32_t pass = 0; pass < 40 && !settled; pass++ ) {

			bool changed = false;

			if ( !layout ( changed ) )
				return false;

			settled = pass > 0 && !changed;

		}

		if ( !settled )
			return fail ( "jump sizes never settled" );

		final = true;

		bool changed = false;

		if ( !layout ( changed ) )
			return false;

		if ( changed )
			return fail ( "layout changed in the last pass" );

		out.origin = origin;

		for ( int32_t i = 0; i < definitions.count ( ); i++ ) {

			int64_t value = 0;
			bool known = true;

			if ( !symbol_value ( i, value, known ) )
				return false;

			symbol entry = { };

			copy_text ( entry.name, sizeof ( entry.name ), &names [ definitions [ i ].name ], definitions [ i ].length );
			entry.value = static_cast < uint32_t > ( value );
			out.symbols.push ( entry );

		}

		for ( const statement& item : statements ) {

			if ( item.size && item.count && !same ( tokens [ item.first ], "org" ) && !same ( tokens [ item.first ], "align" ) )
				out.lines.push ( { item.address, item.line } );

		}

		return true;

	}

	bool assemble ( const char* text, int32_t length, const blob* blobs, int32_t blob_count, result& out ) {

		program* work = new program ( );
		bool ok = work->run ( text, length, blobs, blob_count, out );

		delete work;

		return ok;

	}

	bool find ( const result& image, const char* name, uint32_t& value ) {

		for ( const symbol& entry : image.symbols ) {

			if ( base::same_text ( entry.name, name ) ) {

				value = entry.value;

				return true;

			}

		}

		return false;

	}

}