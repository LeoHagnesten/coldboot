#include "pc/uefi_parts.hh"
#include "pc/fat.hh"

namespace pc {

	using namespace efi;

	void uefi::wait_more ( hv::cpu_state& cpu, uint32_t id, bool& handled ) {

		handled = false;
		check_timers ( );

		if ( !pending.empty ( ) && tpl < 31 ) {

			frame item;

			item.kind = FRAME_RETRY;
			item.status = id;
			item.saved = cpu;
			frames.push ( item );

			callback next = pending [ 0 ];

			pending.remove_at ( 0 );
			start_callback ( cpu, next.function, next.first, next.second );

			return;

		}

		Sleep ( 2 );
		retry ( cpu, id );

	}

	uint32_t uefi::path_length ( hv::cpu_state& cpu, uint64_t address ) {

		uint32_t total = 0;

		while ( address && total < 4096 ) {

			uint8_t node [ 4 ];

			get ( cpu, address + total, node, 4 );

			uint32_t length = static_cast < uint32_t > ( node [ 2 ] | node [ 3 ] << 8 );

			if ( length < 4 )
				return 0;

			total += length;

			if ( node [ 0 ] == 0x7F && node [ 1 ] == 0xFF )
				return total;

		}

		return 0;

	}

	bool uefi::has_protocol ( uint64_t handle, const uint8_t* guid, uint64_t& interface ) const {

		for ( const installed& item : protocols ) {

			if ( item.handle == handle && same_guid ( item.guid, guid ) ) {

				interface = item.interface;

				return true;

			}

		}

		return false;

	}

	void uefi::collect_handles ( hv::cpu_state& cpu, uint32_t kind, uint64_t guid_address, base::list < uint64_t >& out ) {

		uint8_t guid [ 16 ] = { };

		out.clear ( );

		if ( kind == 1 )
			return;

		if ( kind == 2 ) {

			if ( !guid_address )
				return;

			get ( cpu, guid_address, guid, 16 );

		}

		for ( const installed& item : protocols ) {

			if ( kind == 2 && !same_guid ( item.guid, guid ) )
				continue;

			bool seen = false;

			for ( uint64_t handle : out )
				seen = seen || handle == item.handle;

			if ( !seen )
				out.push ( item.handle );

		}

	}

	uint64_t uefi::match_device ( hv::cpu_state& cpu, uint64_t path, const uint8_t* guid, uint32_t& matched ) {

		uint32_t wanted_length = path_length ( cpu, path );
		uint64_t best = 0;

		matched = 0;

		if ( !wanted_length )
			return 0;

		uint8_t* wanted = static_cast < uint8_t* > ( runtime_allocate ( wanted_length ) );

		get ( cpu, path, wanted, wanted_length );

		for ( const installed& item : protocols ) {

			if ( guid && !same_guid ( item.guid, guid ) )
				continue;

			uint64_t device_path = 0;

			if ( !has_protocol ( item.handle, guid_device_path, device_path ) || !device_path )
				continue;

			uint32_t length = path_length ( cpu, device_path );

			if ( length < 4 || length - 4 > wanted_length - 4 || ( best && length - 4 <= matched ) )
				continue;

			uint8_t* candidate = static_cast < uint8_t* > ( runtime_allocate ( length ) );
			bool same = true;

			get ( cpu, device_path, candidate, length );

			for ( uint32_t i = 0; i + 4 < length && same; i++ )
				same = candidate [ i ] == wanted [ i ];

			runtime_release ( candidate );

			if ( same ) {

				best = item.handle;
				matched = length - 4;

			}

		}

		runtime_release ( wanted );

		return best;

	}

	bool uefi::read_file ( const block_device& device, const char* name, base::list < uint8_t >& out ) {

		fat_reader* reader = new fat_reader ( );
		bool ok = reader->open ( device.media, device.offset, device.blocks * device.block_size ) && reader->read_file ( name, out );

		delete reader;

		return ok;

	}

	uint64_t uefi::boot_service ( uint32_t id, hv::cpu_state& cpu, bool& handled ) {

		switch ( id ) {

			case BS_RAISE_TPL: {

				uint64_t old = tpl;

				tpl = argument ( cpu, 0 );

				return old;

			}

			case BS_RESTORE_TPL:
				tpl = argument ( cpu, 0 );
				return 0;

			case BS_ALLOCATE_PAGES: {

				uint32_t kind = static_cast < uint32_t > ( argument ( cpu, 0 ) );
				uint32_t type = static_cast < uint32_t > ( argument ( cpu, 1 ) );
				uint64_t pages = argument ( cpu, 2 );
				uint64_t where = argument ( cpu, 3 );

				if ( !where || type == MEMORY_FREE || ( type > 14 && type < 0x70000000u ) )
					return INVALID_PARAMETER;

				uint64_t current = get64 ( cpu, where );
				uint64_t address = 0;

				if ( kind == 0 )
					address = allocate ( type, pages, ~0ull, ~0ull );
				else if ( kind == 1 )
					address = allocate ( type, pages, current == ~0ull ? ~0ull : current + 1, ~0ull );
				else if ( kind == 2 )
					address = allocate ( type, pages, ~0ull, current );
				else
					return INVALID_PARAMETER;

				if ( !address )
					return kind == 2 ? NOT_FOUND : OUT_OF_RESOURCES;

				put64 ( cpu, where, address );

				return SUCCESS;

			}

			case BS_FREE_PAGES:
				return release_pages ( argument ( cpu, 0 ), argument ( cpu, 1 ) ) ? SUCCESS : NOT_FOUND;

			case BS_GET_MEMORY_MAP: {

				uint64_t size_at = argument ( cpu, 0 );
				uint64_t map = argument ( cpu, 1 );
				uint64_t key_at = argument ( cpu, 2 );
				uint64_t descriptor_at = argument ( cpu, 3 );
				uint64_t version_at = argument ( cpu, 4 );
				base::list < region > merged;

				if ( !size_at )
					return INVALID_PARAMETER;

				for ( const region& item : regions ) {

					if ( !merged.empty ( ) && merged.back ( ).type == item.type && merged.back ( ).start + merged.back ( ).pages * page == item.start )
						merged.back ( ).pages += item.pages;
					else
						merged.push ( item );

				}

				uint64_t needed = static_cast < uint64_t > ( merged.count ( ) ) * 48;
				uint64_t room = get64 ( cpu, size_at );

				put64 ( cpu, size_at, needed );

				if ( descriptor_at )
					put64 ( cpu, descriptor_at, 48 );

				if ( version_at )
					put32 ( cpu, version_at, 1 );

				if ( room < needed || !map )
					return BUFFER_TOO_SMALL;

				uint8_t* table = static_cast < uint8_t* > ( runtime_allocate_zero ( needed ) );

				for ( int32_t i = 0; i < merged.count ( ); i++ ) {

					uint8_t* entry = table + i * 48;
					const region& item = merged [ i ];
					uint64_t attribute = 0xF;

					if ( item.type == MEMORY_RUNTIME_CODE || item.type == MEMORY_RUNTIME_DATA )
						attribute |= 0x8000000000000000ull;

					*reinterpret_cast < uint32_t* > ( entry ) = item.type;
					*reinterpret_cast < uint64_t* > ( entry + 8 ) = item.start;
					*reinterpret_cast < uint64_t* > ( entry + 24 ) = item.pages;
					*reinterpret_cast < uint64_t* > ( entry + 32 ) = attribute;

				}

				put ( cpu, map, table, needed );
				runtime_release ( table );

				if ( key_at )
					put64 ( cpu, key_at, map_key );

				return SUCCESS;

			}

			case BS_ALLOCATE_POOL: {

				uint64_t out = argument ( cpu, 2 );

				if ( !out )
					return INVALID_PARAMETER;

				uint64_t address = pool ( static_cast < uint32_t > ( argument ( cpu, 0 ) ), argument ( cpu, 1 ) );

				if ( !address )
					return OUT_OF_RESOURCES;

				put64 ( cpu, out, address );

				return SUCCESS;

			}

			case BS_FREE_POOL: {

				uint64_t address = argument ( cpu, 0 );

				for ( auto& slot : big_pools ) {

					if ( slot [ 0 ] == address && address ) {

						release_pages ( slot [ 0 ], slot [ 1 ] );
						slot [ 0 ] = 0;
						slot [ 1 ] = 0;

					}

				}

				return SUCCESS;

			}

			case BS_CREATE_EVENT:
			case BS_CREATE_EVENT_EX: {

				uint32_t type = static_cast < uint32_t > ( argument ( cpu, 0 ) );
				uint64_t out = argument ( cpu, id == BS_CREATE_EVENT_EX ? 5 : 4 );
				event item;

				if ( !out )
					return INVALID_PARAMETER;

				item.handle = heap ( 16 );
				item.type = type;
				item.function = argument ( cpu, 2 );
				item.context = argument ( cpu, 3 );

				if ( type == 0x201 )
					item.group = 1;
				else if ( type == 0x60000202 )
					item.group = 2;

				if ( id == BS_CREATE_EVENT_EX && argument ( cpu, 4 ) ) {

					uint8_t guid [ 16 ];

					get ( cpu, argument ( cpu, 4 ), guid, 16 );

					if ( same_guid ( guid, guid_exit_group ) )
						item.group = 1;
					else if ( same_guid ( guid, guid_virtual_group ) )
						item.group = 2;
					else
						item.group = 3;

				}

				if ( !item.handle )
					return OUT_OF_RESOURCES;

				events.push ( item );
				put64 ( cpu, out, item.handle );

				return SUCCESS;

			}

			case BS_SET_TIMER: {

				event* item = find_event ( argument ( cpu, 0 ) );
				uint64_t kind = argument ( cpu, 1 );
				uint64_t trigger = argument ( cpu, 2 );

				if ( !item || !( item->type & 0x80000000u ) )
					return INVALID_PARAMETER;

				if ( kind == 0 ) {

					item->due = 0;
					item->period = 0;

					return SUCCESS;

				}

				uint64_t now = GetTickCount64 ( ) * 10000;

				item->due = now + ( trigger ? trigger : 1 );
				item->period = kind == 1 ? ( trigger < 10000 ? 10000 : trigger ) : 0;

				return SUCCESS;

			}

			case BS_WAIT_FOR_EVENT: {

				uint64_t count = argument ( cpu, 0 );
				uint64_t list = argument ( cpu, 1 );
				uint64_t index_at = argument ( cpu, 2 );

				if ( !count || !list )
					return INVALID_PARAMETER;

				pump_keyboard ( );
				check_timers ( );

				for ( uint64_t i = 0; i < count; i++ ) {

					event* item = find_event ( get64 ( cpu, list + i * 8 ) );

					if ( !item || ( item->type & 0x200 ) ) {

						if ( index_at )
							put64 ( cpu, index_at, i );

						return INVALID_PARAMETER;

					}

					bool ready = item->keyboard ? key_count > 0 || answer_prompt : item->signaled;

					if ( ready ) {

						if ( !item->keyboard )
							item->signaled = false;

						if ( index_at )
							put64 ( cpu, index_at, i );

						return SUCCESS;

					}

				}

				wait_more ( cpu, BS_WAIT_FOR_EVENT, handled );

				return 0;

			}

			case BS_SIGNAL_EVENT: {

				event* item = find_event ( argument ( cpu, 0 ) );

				if ( !item )
					return INVALID_PARAMETER;

				if ( item->group == 3 || !item->group )
					signal ( *item );
				else {

					uint8_t group = item->group;

					for ( event& other : events ) {

						if ( other.group == group )
							signal ( other );

					}

				}

				return SUCCESS;

			}

			case BS_CLOSE_EVENT: {

				event* item = find_event ( argument ( cpu, 0 ) );

				if ( !item )
					return INVALID_PARAMETER;

				item->closed = true;

				return SUCCESS;

			}

			case BS_CHECK_EVENT: {

				event* item = find_event ( argument ( cpu, 0 ) );

				if ( !item || ( item->type & 0x200 ) )
					return INVALID_PARAMETER;

				pump_keyboard ( );
				check_timers ( );

				if ( item->keyboard )
					return key_count || answer_prompt ? SUCCESS : NOT_READY;

				if ( !item->signaled )
					return NOT_READY;

				item->signaled = false;

				return SUCCESS;

			}

			case BS_INSTALL_PROTOCOL: {

				uint64_t handle_at = argument ( cpu, 0 );
				uint64_t guid_at = argument ( cpu, 1 );
				uint8_t guid [ 16 ];
				uint64_t existing = 0;

				if ( !handle_at || !guid_at || argument ( cpu, 2 ) )
					return INVALID_PARAMETER;

				get ( cpu, guid_at, guid, 16 );

				uint64_t handle = get64 ( cpu, handle_at );

				if ( handle && has_protocol ( handle, guid, existing ) )
					return INVALID_PARAMETER;

				if ( !handle ) {

					handle = new_handle ( );
					put64 ( cpu, handle_at, handle );

				}

				install_protocol ( handle, guid, argument ( cpu, 3 ) );

				return SUCCESS;

			}

			case BS_REINSTALL_PROTOCOL:
			case BS_UNINSTALL_PROTOCOL: {

				uint64_t handle = argument ( cpu, 0 );
				uint64_t old = argument ( cpu, 2 );
				uint8_t guid [ 16 ];

				get ( cpu, argument ( cpu, 1 ), guid, 16 );

				for ( int32_t i = 0; i < protocols.count ( ); i++ ) {

					installed& item = protocols [ i ];

					if ( item.handle != handle || !same_guid ( item.guid, guid ) || item.interface != old )
						continue;

					if ( id == BS_REINSTALL_PROTOCOL )
						item.interface = argument ( cpu, 3 );
					else
						protocols.remove_at ( static_cast < size_t > ( i ) );

					return SUCCESS;

				}

				return NOT_FOUND;

			}

			case BS_HANDLE_PROTOCOL:
			case BS_OPEN_PROTOCOL: {

				uint64_t handle = argument ( cpu, 0 );
				uint64_t guid_at = argument ( cpu, 1 );
				uint64_t out = argument ( cpu, 2 );
				uint8_t guid [ 16 ];
				uint64_t interface = 0;

				if ( !guid_at || ( !out && ( id == BS_HANDLE_PROTOCOL || argument ( cpu, 5 ) != 4 ) ) )
					return INVALID_PARAMETER;

				get ( cpu, guid_at, guid, 16 );

				if ( !known_handle ( handle ) )
					return id == BS_OPEN_PROTOCOL ? INVALID_PARAMETER : UNSUPPORTED;

				if ( !has_protocol ( handle, guid, interface ) ) {

					if ( out && ( id == BS_HANDLE_PROTOCOL || argument ( cpu, 5 ) != 4 ) )
						put64 ( cpu, out, 0 );

					return UNSUPPORTED;

				}

				if ( out && ( id == BS_HANDLE_PROTOCOL || argument ( cpu, 5 ) != 4 ) )
					put64 ( cpu, out, interface );

				return SUCCESS;

			}

			case BS_RESERVED:
				return UNSUPPORTED;

			case BS_REGISTER_NOTIFY:

				if ( argument ( cpu, 2 ) )
					put64 ( cpu, argument ( cpu, 2 ), heap ( 16 ) );

				return SUCCESS;

			case BS_LOCATE_HANDLE: {

				uint64_t size_at = argument ( cpu, 3 );
				uint64_t buffer = argument ( cpu, 4 );
				base::list < uint64_t > found;

				if ( !size_at )
					return INVALID_PARAMETER;

				collect_handles ( cpu, static_cast < uint32_t > ( argument ( cpu, 0 ) ), argument ( cpu, 1 ), found );

				if ( found.empty ( ) )
					return NOT_FOUND;

				uint64_t needed = found.size ( ) * 8;
				uint64_t room = get64 ( cpu, size_at );

				put64 ( cpu, size_at, needed );

				if ( room < needed || !buffer )
					return BUFFER_TOO_SMALL;

				put ( cpu, buffer, found.data ( ), needed );

				return SUCCESS;

			}

			case BS_LOCATE_HANDLE_BUFFER: {

				uint64_t count_at = argument ( cpu, 3 );
				uint64_t buffer_at = argument ( cpu, 4 );
				base::list < uint64_t > found;

				if ( !count_at || !buffer_at )
					return INVALID_PARAMETER;

				collect_handles ( cpu, static_cast < uint32_t > ( argument ( cpu, 0 ) ), argument ( cpu, 1 ), found );
				put64 ( cpu, count_at, found.size ( ) );

				if ( found.empty ( ) ) {

					put64 ( cpu, buffer_at, 0 );

					return NOT_FOUND;

				}

				uint64_t buffer = pool ( MEMORY_BOOT_DATA, found.size ( ) * 8 );

				put ( cpu, buffer, found.data ( ), found.size ( ) * 8 );
				put64 ( cpu, buffer_at, buffer );

				return SUCCESS;

			}

			case BS_LOCATE_DEVICE_PATH: {

				uint64_t guid_at = argument ( cpu, 0 );
				uint64_t path_at = argument ( cpu, 1 );
				uint64_t device_at = argument ( cpu, 2 );
				uint8_t guid [ 16 ];
				uint32_t matched = 0;

				if ( !guid_at || !path_at || !device_at )
					return INVALID_PARAMETER;

				get ( cpu, guid_at, guid, 16 );

				uint64_t path = get64 ( cpu, path_at );
				uint64_t handle = match_device ( cpu, path, guid, matched );

				if ( !handle )
					return NOT_FOUND;

				put64 ( cpu, device_at, handle );
				put64 ( cpu, path_at, path + matched );

				return SUCCESS;

			}

			case BS_INSTALL_CONFIGURATION: {

				uint64_t guid_at = argument ( cpu, 0 );
				uint64_t table = argument ( cpu, 1 );
				uint8_t guid [ 16 ];

				if ( !guid_at )
					return INVALID_PARAMETER;

				get ( cpu, guid_at, guid, 16 );

				for ( uint32_t i = 0; i < config_count; i++ ) {

					uint8_t current [ 16 ];

					vm->read ( config_table + i * 24ull, current, 16 );

					if ( !same_guid ( current, guid ) )
						continue;

					if ( table )
						poke64 ( config_table + i * 24ull + 16, table );
					else {

						uint8_t moved [ 24 ];

						for ( uint32_t j = i + 1; j < config_count; j++ ) {

							vm->read ( config_table + j * 24ull, moved, 24 );
							poke ( config_table + ( j - 1 ) * 24ull, moved, 24 );

						}

						config_count--;

					}

					poke64 ( system_table + 104, config_count );

					return SUCCESS;

				}

				if ( !table )
					return NOT_FOUND;

				if ( config_count >= 16 )
					return OUT_OF_RESOURCES;

				poke ( config_table + config_count * 24ull, guid, 16 );
				poke64 ( config_table + config_count * 24ull + 16, table );
				config_count++;
				poke64 ( system_table + 104, config_count );

				return SUCCESS;

			}

			case BS_LOAD_IMAGE: {

				uint64_t parent = argument ( cpu, 1 );
				uint64_t path = argument ( cpu, 2 );
				uint64_t source = argument ( cpu, 3 );
				uint64_t size = argument ( cpu, 4 );
				uint64_t out = argument ( cpu, 5 );
				base::list < uint8_t > file;
				uint64_t device = 0;
				uint32_t matched = 0;
				char why [ 64 ] = { };

				if ( !out )
					return INVALID_PARAMETER;

				if ( path )
					device = match_device ( cpu, path, nullptr, matched );

				uint32_t total = path ? path_length ( cpu, path ) : 0;
				uint32_t rest_size = total > matched ? total - matched : 0;
				base::list < uint8_t > rest;

				rest.resize ( rest_size );

				if ( rest_size )
					get ( cpu, path + matched, rest.data ( ), rest_size );

				if ( source ) {

					if ( !size || size > 0x10000000 )
						return LOAD_ERROR;

					file.resize ( size );
					get ( cpu, source, file.data ( ), size );

				} else {

					const block_device* owner = nullptr;

					for ( const block_device& item : disks ) {

						if ( item.handle == device )
							owner = &item;

					}

					char name [ 260 ] = { };
					int32_t used = 0;

					for ( uint32_t at = 0; at + 4 <= rest_size; ) {

						uint32_t length = static_cast < uint32_t > ( rest [ at + 2 ] | rest [ at + 3 ] << 8 );

						if ( length < 4 || rest [ at ] == 0x7F )
							break;

						if ( rest [ at ] == 4 && rest [ at + 1 ] == 4 ) {

							if ( used && name [ used - 1 ] != '\\' && used < 258 )
								name [ used++ ] = '\\';

							for ( uint32_t c = 4; c + 1 < length && used < 258; c += 2 ) {

								uint16_t value = static_cast < uint16_t > ( rest [ at + c ] | rest [ at + c + 1 ] << 8 );

								if ( !value )
									break;

								name [ used++ ] = static_cast < char > ( value < 0x80 ? value : '_' );

							}

						}

						at += length;

					}

					if ( !owner || !used || !read_file ( *owner, name, file ) )
						return NOT_FOUND;

				}

				uint64_t handle = 0;

				if ( !load_image ( file.data ( ), static_cast < uint32_t > ( file.size ( ) ), parent, device, rest.data ( ), rest_size, handle, why ) ) {

					log ( "LoadImage failed: " );
					log ( why );
					log ( "\n" );

					return LOAD_ERROR;

				}

				put64 ( cpu, out, handle );

				return SUCCESS;

			}

			case BS_START_IMAGE: {

				uint64_t handle = argument ( cpu, 0 );

				for ( const image& item : images ) {

					if ( item.handle != handle )
						continue;

					frame entry;

					entry.kind = FRAME_IMAGE;
					entry.image = handle;
					entry.saved = cpu;
					frames.push ( entry );
					start_callback ( cpu, item.entry, handle, system_table );
					handled = false;

					return 0;

				}

				return INVALID_PARAMETER;

			}

			case BS_EXIT: {

				uint64_t handle = argument ( cpu, 0 );

				for ( int32_t i = frames.count ( ) - 1; i >= 0; i-- ) {

					if ( ( frames [ i ].kind != FRAME_IMAGE && frames [ i ].kind != FRAME_MAIN ) || frames [ i ].image != handle )
						continue;

					frames.resize ( static_cast < size_t > ( i ) + 1 );

					const hv::cpu_state& saved = frames [ i ].saved;
					static const uint32_t kept [ 8 ] = { 3, 5, 6, 7, 12, 13, 14, 15 };

					for ( uint32_t index : kept )
						cpu.gpr [ index ] = saved.gpr [ index ];

					cpu.gpr [ hv::REG_SP ] = saved.gpr [ hv::REG_SP ] - 0x28;
					cpu.gpr [ hv::REG_AX ] = argument ( cpu, 1 );
					cpu.rip = stubs + dispatch_resume;
					handled = false;

					return 0;

				}

				return INVALID_PARAMETER;

			}

			case BS_UNLOAD_IMAGE: {

				uint64_t handle = argument ( cpu, 0 );

				for ( int32_t i = 0; i < images.count ( ); i++ ) {

					if ( images [ i ].handle != handle )
						continue;

					release_pages ( images [ i ].base, images [ i ].pages );

					for ( int32_t p = protocols.count ( ) - 1; p >= 0; p-- ) {

						if ( protocols [ p ].handle == handle )
							protocols.remove_at ( static_cast < size_t > ( p ) );

					}

					images.remove_at ( static_cast < size_t > ( i ) );

					return SUCCESS;

				}

				return INVALID_PARAMETER;

			}

			case BS_EXIT_BOOT_SERVICES: {

				if ( argument ( cpu, 1 ) != map_key )
					return INVALID_PARAMETER;

				exited = true;
				copy_text ( last_boot, "the operating system took over from UEFI", sizeof ( last_boot ) );
				log ( "ExitBootServices\n" );

				for ( event& item : events ) {

					if ( item.group == 1 )
						signal ( item );

				}

				return SUCCESS;

			}

			case BS_MONOTONIC:

				if ( !argument ( cpu, 0 ) )
					return INVALID_PARAMETER;

				put64 ( cpu, argument ( cpu, 0 ), ++monotonic );

				return SUCCESS;

			case BS_STALL: {

				uint64_t micro = argument ( cpu, 0 );

				if ( micro >= 1000 )
					Sleep ( static_cast < DWORD > ( micro / 1000 ) );
				else {

					LARGE_INTEGER frequency;
					LARGE_INTEGER start;
					LARGE_INTEGER now;

					QueryPerformanceFrequency ( &frequency );
					QueryPerformanceCounter ( &start );

					do
						QueryPerformanceCounter ( &now );
					while ( static_cast < uint64_t > ( now.QuadPart - start.QuadPart ) * 1000000 / static_cast < uint64_t > ( frequency.QuadPart ) < micro );

				}

				return SUCCESS;

			}

			case BS_WATCHDOG:
			case BS_CONNECT:
			case BS_DISCONNECT:
			case BS_CLOSE_PROTOCOL:
				return SUCCESS;

			case BS_PROTOCOL_INFORMATION:

				if ( argument ( cpu, 2 ) )
					put64 ( cpu, argument ( cpu, 2 ), pool ( MEMORY_BOOT_DATA, 16 ) );

				if ( argument ( cpu, 3 ) )
					put64 ( cpu, argument ( cpu, 3 ), 0 );

				return SUCCESS;

			case BS_PROTOCOLS_PER_HANDLE: {

				uint64_t handle = argument ( cpu, 0 );
				uint64_t array_at = argument ( cpu, 1 );
				uint64_t count_at = argument ( cpu, 2 );
				base::list < const installed* > found;

				for ( const installed& item : protocols ) {

					if ( item.handle == handle )
						found.push ( &item );

				}

				if ( found.empty ( ) || !array_at || !count_at )
					return INVALID_PARAMETER;

				uint64_t count = found.size ( );
				uint64_t buffer = pool ( MEMORY_BOOT_DATA, count * 24 );

				for ( uint64_t i = 0; i < count; i++ ) {

					put64 ( cpu, buffer + i * 8, buffer + count * 8 + i * 16 );
					put ( cpu, buffer + count * 8 + i * 16, found [ i ]->guid, 16 );

				}

				put64 ( cpu, array_at, buffer );
				put64 ( cpu, count_at, count );

				return SUCCESS;

			}

			case BS_LOCATE_PROTOCOL: {

				uint64_t guid_at = argument ( cpu, 0 );
				uint64_t out = argument ( cpu, 2 );
				uint8_t guid [ 16 ];

				if ( !guid_at || !out )
					return INVALID_PARAMETER;

				get ( cpu, guid_at, guid, 16 );

				for ( const installed& item : protocols ) {

					if ( same_guid ( item.guid, guid ) ) {

						put64 ( cpu, out, item.interface );

						return SUCCESS;

					}

				}

				put64 ( cpu, out, 0 );

				return NOT_FOUND;

			}

			case BS_INSTALL_MULTIPLE:
			case BS_UNINSTALL_MULTIPLE: {

				uint64_t handle_at = argument ( cpu, 0 );
				uint64_t handle = id == BS_INSTALL_MULTIPLE ? ( handle_at ? get64 ( cpu, handle_at ) : 0 ) : handle_at;

				if ( id == BS_INSTALL_MULTIPLE && !handle_at )
					return INVALID_PARAMETER;

				if ( !handle && id == BS_INSTALL_MULTIPLE ) {

					handle = new_handle ( );
					put64 ( cpu, handle_at, handle );

				}

				for ( uint32_t i = 1; i < 64; i += 2 ) {

					uint64_t guid_at = argument ( cpu, i );

					if ( !guid_at )
						break;

					uint8_t guid [ 16 ];

					get ( cpu, guid_at, guid, 16 );

					if ( id == BS_INSTALL_MULTIPLE )
						install_protocol ( handle, guid, argument ( cpu, i + 1 ) );
					else {

						for ( int32_t p = protocols.count ( ) - 1; p >= 0; p-- ) {

							if ( protocols [ p ].handle == handle && same_guid ( protocols [ p ].guid, guid ) )
								protocols.remove_at ( static_cast < size_t > ( p ) );

						}

					}

				}

				return SUCCESS;

			}

			case BS_CRC32: {

				uint64_t data = argument ( cpu, 0 );
				uint64_t size = argument ( cpu, 1 );
				uint64_t out = argument ( cpu, 2 );

				if ( !data || !size || !out || size > 0x4000000 )
					return INVALID_PARAMETER;

				uint8_t* bytes = static_cast < uint8_t* > ( runtime_allocate ( size ) );

				get ( cpu, data, bytes, size );
				put32 ( cpu, out, crc32 ( bytes, size, 0 ) );
				runtime_release ( bytes );

				return SUCCESS;

			}

			case BS_COPY_MEMORY: {

				uint64_t target = argument ( cpu, 0 );
				uint64_t source = argument ( cpu, 1 );
				uint64_t size = argument ( cpu, 2 );
				uint64_t chunk_room = 0x100000;
				uint8_t* chunk = static_cast < uint8_t* > ( runtime_allocate ( chunk_room ) );
				bool backward = target > source && target < source + size;

				for ( uint64_t done = 0; done < size; ) {

					uint64_t take = size - done < chunk_room ? size - done : chunk_room;
					uint64_t at = backward ? size - done - take : done;

					get ( cpu, source + at, chunk, take );
					put ( cpu, target + at, chunk, take );
					done += take;

				}

				runtime_release ( chunk );

				return 0;

			}

			case BS_SET_MEMORY: {

				uint64_t target = argument ( cpu, 0 );
				uint64_t size = argument ( cpu, 1 );
				uint8_t value = static_cast < uint8_t > ( argument ( cpu, 2 ) );
				uint64_t chunk_room = size < 0x100000 ? size : 0x100000;
				uint8_t* chunk = static_cast < uint8_t* > ( runtime_allocate ( chunk_room ? chunk_room : 1 ) );

				for ( uint64_t i = 0; i < chunk_room; i++ )
					chunk [ i ] = value;

				for ( uint64_t done = 0; done < size; ) {

					uint64_t take = size - done < chunk_room ? size - done : chunk_room;

					put ( cpu, target + done, chunk, take );
					done += take;

				}

				runtime_release ( chunk );

				return 0;

			}

			default:
				return UNSUPPORTED;

		}

	}

}