#include "pc/tools.hh"

namespace pc {

	static uint32_t le32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24;

	}

	static uint64_t le64 ( const uint8_t* at ) {

		return static_cast < uint64_t > ( le32 ( at ) ) | static_cast < uint64_t > ( le32 ( at + 4 ) ) << 32;

	}

	static void join ( wchar_t* out, const wchar_t* folder, const wchar_t* name ) {

		int32_t used = 0;

		for ( int32_t i = 0; folder [ i ] && used < MAX_PATH - 2; i++ )
			out [ used++ ] = folder [ i ];

		out [ used++ ] = L'\\';

		for ( int32_t i = 0; name [ i ] && used < MAX_PATH - 1; i++ )
			out [ used++ ] = name [ i ];

		out [ used ] = 0;

	}

	static void make_folders ( const wchar_t* path, bool last ) {

		wchar_t partial [ MAX_PATH ];
		int32_t length = 0;

		while ( path [ length ] && length < MAX_PATH - 1 )
			length++;

		for ( int32_t i = 0; i <= length; i++ ) {

			partial [ i ] = path [ i ];

			if ( ( path [ i ] == L'\\' && i > 2 ) || ( i == length && last ) ) {

				partial [ i ] = 0;
				CreateDirectoryW ( partial, nullptr );
				partial [ i ] = path [ i ];

			}

		}

	}

	bool tools_clean_name ( const wchar_t* name, uint32_t length, wchar_t* out, uint32_t room ) {

		uint32_t used = 0;
		uint32_t part = 0;
		uint32_t depth = 0;

		for ( uint32_t i = 0; i <= length; i++ ) {

			wchar_t c = i < length ? name [ i ] : L'\\';

			if ( c == L'/' )
				c = L'\\';

			if ( c == L'\\' ) {

				if ( !part )
					return false;

				const wchar_t* start = out + used - part;

				if ( ( part == 1 && start [ 0 ] == L'.' ) || ( part == 2 && start [ 0 ] == L'.' && start [ 1 ] == L'.' ) || start [ part - 1 ] == L'.' || start [ part - 1 ] == L' ' )
					return false;

				if ( ++depth > 32 )
					return false;

				if ( i < length ) {

					if ( used + 1 >= room )
						return false;

					out [ used++ ] = L'\\';

				}

				part = 0;

				continue;

			}

			if ( c < 32 || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|' || part >= 200 || used + 1 >= room )
				return false;

			out [ used++ ] = c;
			part++;

		}

		out [ used ] = 0;

		return used > 0;

	}

	bool tools_collect ( const wchar_t* source, const wchar_t* name, base::list < tools_entry >& out, uint64_t& total ) {

		WIN32_FILE_ATTRIBUTE_DATA data;

		if ( out.count ( ) > 100000 || !GetFileAttributesExW ( source, GetFileExInfoStandard, &data ) )
			return false;

		tools_entry& item = out.emplace ( );
		int32_t length = 0;

		for ( ; source [ length ] && length < MAX_PATH - 1; length++ )
			item.source [ length ] = source [ length ];

		for ( length = 0; name [ length ] && length < 259; length++ )
			item.name [ length ] = name [ length ];

		item.folder = ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0;
		item.size = item.folder ? 0 : static_cast < uint64_t > ( data.nFileSizeHigh ) << 32 | data.nFileSizeLow;
		total += item.size;

		if ( !item.folder )
			return true;

		wchar_t pattern [ MAX_PATH ];
		WIN32_FIND_DATAW found;

		join ( pattern, source, L"*" );

		HANDLE search = FindFirstFileW ( pattern, &found );

		if ( search == INVALID_HANDLE_VALUE )
			return true;

		do {

			if ( found.cFileName [ 0 ] == L'.' && ( !found.cFileName [ 1 ] || ( found.cFileName [ 1 ] == L'.' && !found.cFileName [ 2 ] ) ) )
				continue;

			wchar_t child [ MAX_PATH ];
			wchar_t child_name [ 260 ];

			join ( child, source, found.cFileName );

			int32_t used = 0;

			for ( ; name [ used ] && used < 258; used++ )
				child_name [ used ] = name [ used ];

			child_name [ used++ ] = L'\\';

			for ( int32_t i = 0; found.cFileName [ i ] && used < 259; i++ )
				child_name [ used++ ] = found.cFileName [ i ];

			child_name [ used ] = 0;
			tools_collect ( child, child_name, out, total );

		} while ( FindNextFileW ( search, &found ) );

		FindClose ( search );

		return true;

	}

	guest_tools::~guest_tools ( ) {

		stop_sending ( );
		finish_receive ( false );
		runtime_release ( chunk );

	}

	void guest_tools::frame ( uint32_t type, const void* data, uint32_t size ) {

		uint8_t header [ 8 ] = { static_cast < uint8_t > ( type ), static_cast < uint8_t > ( type >> 8 ), static_cast < uint8_t > ( type >> 16 ), static_cast < uint8_t > ( type >> 24 ), static_cast < uint8_t > ( size ), static_cast < uint8_t > ( size >> 8 ), static_cast < uint8_t > ( size >> 16 ), static_cast < uint8_t > ( size >> 24 ) };
		size_t start = outbound.size ( );

		outbound.resize ( start + 8 + size );

		for ( int32_t i = 0; i < 8; i++ )
			outbound [ start + i ] = header [ i ];

		for ( uint32_t i = 0; i < size; i++ )
			outbound [ start + 8 + i ] = static_cast < const uint8_t* > ( data ) [ i ];

	}

	void guest_tools::push_event ( tools_event&& item ) {

		if ( events.count ( ) < 64 )
			events.push ( base::move ( item ) );

	}

	void guest_tools::set_policy ( uint32_t flags ) {

		AcquireSRWLockExclusive ( &lock );
		policy = flags;

		if ( linked )
			frame ( TOOLS_POLICY, &policy, 4 );

		ReleaseSRWLockExclusive ( &lock );

		if ( router )
			router->kick ( );

	}

	void guest_tools::set_inbox ( const wchar_t* folder ) {

		AcquireSRWLockExclusive ( &lock );

		int32_t used = 0;

		for ( ; folder && folder [ used ] && used < MAX_PATH - 1; used++ )
			inbox [ used ] = folder [ used ];

		inbox [ used ] = 0;
		ReleaseSRWLockExclusive ( &lock );

	}

	bool guest_tools::connected ( ) {

		AcquireSRWLockShared ( &lock );

		bool result = linked;

		ReleaseSRWLockShared ( &lock );

		return result;

	}

	bool guest_tools::busy ( ) {

		AcquireSRWLockShared ( &lock );

		bool result = send_active;

		ReleaseSRWLockShared ( &lock );

		return result;

	}

	bool guest_tools::send_text ( const wchar_t* text, uint32_t length ) {

		AcquireSRWLockExclusive ( &lock );

		bool allowed = linked && ( policy & ( agent ? SHARE_TEXT_OUT : SHARE_TEXT_IN ) ) && length * 2 < tools_frame_limit;

		if ( allowed )
			frame ( TOOLS_TEXT, text, length * 2 );

		ReleaseSRWLockExclusive ( &lock );

		if ( allowed && router )
			router->kick ( );

		return allowed;

	}

	bool guest_tools::send_files ( const wchar_t ( *paths ) [ MAX_PATH ], int32_t count, uint32_t purpose, uint64_t limit ) {

		base::list < tools_entry > found;
		uint64_t total = 0;

		for ( int32_t i = 0; i < count; i++ ) {

			const wchar_t* leaf = paths [ i ];

			for ( const wchar_t* at = paths [ i ]; *at; at++ ) {

				if ( *at == L'\\' || *at == L'/' )
					leaf = at + 1;

			}

			if ( leaf [ 0 ] )
				tools_collect ( paths [ i ], leaf, found, total );

		}

		if ( found.empty ( ) || total > limit )
			return false;

		AcquireSRWLockExclusive ( &lock );

		bool allowed = linked && ( policy & ( agent ? SHARE_FILES_OUT : SHARE_FILES_IN ) ) && !send_active;

		if ( allowed ) {

			sending = base::move ( found );
			send_index = 0;
			send_purpose = purpose;
			send_total = total;
			send_started = false;
			send_active = true;

		}

		ReleaseSRWLockExclusive ( &lock );

		if ( allowed && router )
			router->kick ( );

		return allowed;

	}

	bool guest_tools::next_event ( tools_event& out ) {

		AcquireSRWLockExclusive ( &lock );

		bool found = !events.empty ( );

		if ( found ) {

			out = base::move ( events [ 0 ] );
			events.remove_at ( 0 );

		}

		ReleaseSRWLockExclusive ( &lock );

		return found;

	}

	void guest_tools::stop_sending ( ) {

		if ( send_file != INVALID_HANDLE_VALUE )
			CloseHandle ( send_file );

		send_file = INVALID_HANDLE_VALUE;
		send_active = false;
		sending.clear ( );

	}

	void guest_tools::finish_receive ( bool keep ) {

		if ( receive_file != INVALID_HANDLE_VALUE )
			CloseHandle ( receive_file );

		receive_file = INVALID_HANDLE_VALUE;

		if ( keep && receiving && receive_allowed && receive_count ) {

			tools_event item;

			item.kind = TOOLS_GOT_FILES;
			item.purpose = receive_purpose;
			item.count = receive_count;
			item.items = base::move ( receive_items );
			item.items.push ( 0 );
			push_event ( base::move ( item ) );

		}

		receive_items.clear ( );
		receive_count = 0;
		receiving = false;

	}

	void guest_tools::opened ( ) {

		AcquireSRWLockExclusive ( &lock );
		stop_sending ( );
		finish_receive ( false );
		linked = false;
		inbound.clear ( );
		outbound.clear ( );
		outbound_head = 0;

		if ( agent ) {

			uint8_t hello [ 4 + 64 * 2 ] = { static_cast < uint8_t > ( tools_version ) };
			wchar_t name [ 64 ] = { };
			DWORD length = 63;

			GetComputerNameW ( name, &length );

			for ( DWORD i = 0; i < length && i < 63; i++ ) {

				hello [ 4 + i * 2 ] = static_cast < uint8_t > ( name [ i ] );
				hello [ 5 + i * 2 ] = static_cast < uint8_t > ( name [ i ] >> 8 );

			}

			frame ( TOOLS_HELLO, hello, 4 + length * 2 );

		}

		ReleaseSRWLockExclusive ( &lock );

	}

	void guest_tools::closed ( ) {

		AcquireSRWLockExclusive ( &lock );

		if ( linked ) {

			tools_event item;

			item.kind = TOOLS_DISCONNECTED;
			push_event ( base::move ( item ) );

		}

		linked = false;
		stop_sending ( );
		finish_receive ( false );
		inbound.clear ( );
		outbound.clear ( );
		outbound_head = 0;
		ReleaseSRWLockExclusive ( &lock );

	}

	void guest_tools::handle ( uint32_t type, const uint8_t* data, uint32_t size ) {

		switch ( type ) {

			case TOOLS_HELLO: {

				if ( agent )
					return;

				uint32_t used = 0;

				for ( uint32_t at = 4; at + 1 < size && used < 63; at += 2 )
					guest_name [ used++ ] = static_cast < wchar_t > ( data [ at ] | data [ at + 1 ] << 8 );

				guest_name [ used ] = 0;
				linked = true;
				frame ( TOOLS_POLICY, &policy, 4 );

				tools_event item;

				item.kind = TOOLS_CONNECTED;
				push_event ( base::move ( item ) );

				return;

			}

			case TOOLS_POLICY: {

				if ( !agent || size < 4 )
					return;

				bool was = linked;

				policy = le32 ( data );
				linked = true;

				if ( !was ) {

					tools_event item;

					item.kind = TOOLS_CONNECTED;
					push_event ( base::move ( item ) );

				}

				return;

			}

			case TOOLS_TEXT: {

				if ( !linked || !( policy & ( agent ? SHARE_TEXT_IN : SHARE_TEXT_OUT ) ) )
					return;

				tools_event item;

				item.kind = TOOLS_GOT_TEXT;
				item.text.resize ( size / 2 + 1 );

				for ( uint32_t i = 0; i < size / 2; i++ )
					item.text [ i ] = static_cast < wchar_t > ( data [ i * 2 ] | data [ i * 2 + 1 ] << 8 );

				item.text [ size / 2 ] = 0;
				push_event ( base::move ( item ) );

				return;

			}

			case TOOLS_FILES_BEGIN: {

				finish_receive ( false );

				if ( size < 16 || !linked )
					return;

				receive_purpose = le32 ( data );
				receive_total = le64 ( data + 8 );
				received_bytes = 0;
				receive_allowed = ( policy & ( agent ? SHARE_FILES_IN : SHARE_FILES_OUT ) ) && ( agent || receive_purpose == PURPOSE_CLIPBOARD ) && ( receive_purpose == PURPOSE_SENT || receive_total <= clipboard_file_limit ) && inbox [ 0 ];
				receiving = true;

				if ( !receive_allowed ) {

					tools_event item;

					item.kind = TOOLS_REFUSED;
					push_event ( base::move ( item ) );

					return;

				}

				wchar_t serial [ 16 ];
				uint32_t number = ++inbox_serial;
				int32_t digits = 0;
				wchar_t reversed [ 16 ];

				do {

					reversed [ digits++ ] = static_cast < wchar_t > ( L'0' + number % 10 );
					number /= 10;

				} while ( number );

				for ( int32_t i = 0; i < digits; i++ )
					serial [ i ] = reversed [ digits - 1 - i ];

				serial [ digits ] = 0;
				join ( receive_folder, inbox, serial );
				make_folders ( receive_folder, true );

				return;

			}

			case TOOLS_FOLDER:
			case TOOLS_FILE: {

				if ( receive_file != INVALID_HANDLE_VALUE )
					CloseHandle ( receive_file );

				receive_file = INVALID_HANDLE_VALUE;
				receive_left = 0;

				if ( !receiving || !receive_allowed )
					return;

				uint32_t skip = type == TOOLS_FILE ? 8 : 0;
				wchar_t raw [ 260 ];
				wchar_t clean [ 260 ];
				uint32_t length = 0;

				if ( size < skip )
					return;

				for ( uint32_t at = skip; at + 1 < size && length < 259; at += 2 )
					raw [ length++ ] = static_cast < wchar_t > ( data [ at ] | data [ at + 1 ] << 8 );

				if ( !tools_clean_name ( raw, length, clean, 260 ) )
					return;

				wchar_t path [ MAX_PATH ];
				bool top = true;

				for ( int32_t i = 0; clean [ i ]; i++ )
					top = top && clean [ i ] != L'\\';

				join ( path, receive_folder, clean );

				if ( top ) {

					for ( int32_t i = 0; path [ i ]; i++ )
						receive_items.push ( path [ i ] );

					receive_items.push ( 0 );
					receive_count++;

				}

				if ( type == TOOLS_FOLDER ) {

					make_folders ( path, true );

					return;

				}

				uint64_t length_bytes = le64 ( data );

				if ( received_bytes + length_bytes > receive_total ) {

					receive_allowed = false;

					return;

				}

				make_folders ( path, false );
				receive_file = CreateFileW ( path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
				receive_left = length_bytes;

				return;

			}

			case TOOLS_DATA: {

				if ( receive_file == INVALID_HANDLE_VALUE || !receive_allowed )
					return;

				uint32_t take = static_cast < uint32_t > ( size < receive_left ? size : receive_left );
				DWORD done = 0;

				if ( take )
					WriteFile ( receive_file, data, take, &done, nullptr );

				receive_left -= take;
				received_bytes += take;

				return;

			}

			case TOOLS_FILES_END:
				finish_receive ( true );
				return;

			default:
				return;

		}

	}

	void guest_tools::received ( const uint8_t* data, uint32_t size ) {

		AcquireSRWLockExclusive ( &lock );

		size_t start = inbound.size ( );

		inbound.resize ( start + size );

		for ( uint32_t i = 0; i < size; i++ )
			inbound [ start + i ] = data [ i ];

		size_t at = 0;

		while ( inbound.size ( ) - at >= 8 ) {

			uint32_t type = le32 ( inbound.data ( ) + at );
			uint32_t length = le32 ( inbound.data ( ) + at + 4 );

			if ( length > tools_frame_limit ) {

				at = inbound.size ( );

				break;

			}

			if ( inbound.size ( ) - at - 8 < length )
				break;

			handle ( type, inbound.data ( ) + at + 8, length );
			at += 8 + length;

		}

		if ( at ) {

			size_t left = inbound.size ( ) - at;

			for ( size_t i = 0; i < left; i++ )
				inbound [ i ] = inbound [ at + i ];

			inbound.resize ( left );

		}

		ReleaseSRWLockExclusive ( &lock );

	}

	uint32_t guest_tools::produce ( uint8_t* out, uint32_t room ) {

		AcquireSRWLockExclusive ( &lock );

		if ( !chunk )
			chunk = static_cast < uint8_t* > ( runtime_allocate ( tools_chunk ) );

		while ( outbound.size ( ) - outbound_head < room && send_active && linked ) {

			if ( !send_started ) {

				uint8_t head [ 16 ] = { };
				uint32_t count = static_cast < uint32_t > ( sending.count ( ) );

				for ( int32_t i = 0; i < 4; i++ ) {

					head [ i ] = static_cast < uint8_t > ( send_purpose >> ( i * 8 ) );
					head [ 4 + i ] = static_cast < uint8_t > ( count >> ( i * 8 ) );

				}

				for ( int32_t i = 0; i < 8; i++ )
					head [ 8 + i ] = static_cast < uint8_t > ( send_total >> ( i * 8 ) );

				frame ( TOOLS_FILES_BEGIN, head, 16 );
				send_started = true;

			} else if ( send_file != INVALID_HANDLE_VALUE ) {

				DWORD got = 0;

				if ( ReadFile ( send_file, chunk, tools_chunk, &got, nullptr ) && got )
					frame ( TOOLS_DATA, chunk, got );
				else {

					CloseHandle ( send_file );
					send_file = INVALID_HANDLE_VALUE;
					send_index++;

				}

			} else if ( send_index < static_cast < uint32_t > ( sending.count ( ) ) ) {

				const tools_entry& item = sending [ send_index ];
				uint32_t name_length = 0;

				while ( item.name [ name_length ] )
					name_length++;

				if ( item.folder ) {

					frame ( TOOLS_FOLDER, item.name, name_length * 2 );
					send_index++;

				} else {

					uint8_t head [ 8 + 520 ];

					send_file = CreateFileW ( item.source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr );

					uint64_t size = send_file == INVALID_HANDLE_VALUE ? 0 : item.size;

					for ( int32_t i = 0; i < 8; i++ )
						head [ i ] = static_cast < uint8_t > ( size >> ( i * 8 ) );

					for ( uint32_t i = 0; i < name_length; i++ ) {

						head [ 8 + i * 2 ] = static_cast < uint8_t > ( item.name [ i ] );
						head [ 9 + i * 2 ] = static_cast < uint8_t > ( item.name [ i ] >> 8 );

					}

					frame ( TOOLS_FILE, head, 8 + name_length * 2 );

					if ( send_file == INVALID_HANDLE_VALUE )
						send_index++;

				}

			} else {

				frame ( TOOLS_FILES_END, nullptr, 0 );
				send_active = false;
				sending.clear ( );

				tools_event item;

				item.kind = TOOLS_SENT;
				item.purpose = send_purpose;
				push_event ( base::move ( item ) );

			}

		}

		uint32_t available = static_cast < uint32_t > ( outbound.size ( ) - outbound_head );
		uint32_t take = available < room ? available : room;

		for ( uint32_t i = 0; i < take; i++ )
			out [ i ] = outbound [ outbound_head + i ];

		outbound_head += take;

		if ( outbound_head == outbound.size ( ) ) {

			outbound.clear ( );
			outbound_head = 0;

		} else if ( outbound_head > 0x100000 ) {

			size_t left = outbound.size ( ) - outbound_head;

			for ( size_t i = 0; i < left; i++ )
				outbound [ i ] = outbound [ outbound_head + i ];

			outbound.resize ( left );
			outbound_head = 0;

		}

		ReleaseSRWLockExclusive ( &lock );

		return take;

	}

}