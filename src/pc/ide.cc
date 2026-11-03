#include "pc/ide.hh"

namespace pc {

	static constexpr uint8_t STATUS_BUSY = 0x80;
	static constexpr uint8_t STATUS_READY = 0x40;
	static constexpr uint8_t STATUS_SEEK = 0x10;
	static constexpr uint8_t STATUS_REQUEST = 0x08;
	static constexpr uint8_t STATUS_ERROR = 0x01;
	static constexpr uint8_t ERROR_ABORT = 0x04;
	static constexpr uint32_t optical_block = 2048;
	static constexpr uint64_t dvd_threshold = 900ull * 1024 * 1024;

	static void put_be16 ( uint8_t* out, uint32_t value ) {

		out [ 0 ] = static_cast < uint8_t > ( value >> 8 );
		out [ 1 ] = static_cast < uint8_t > ( value );

	}

	static void put_be32 ( uint8_t* out, uint32_t value ) {

		out [ 0 ] = static_cast < uint8_t > ( value >> 24 );
		out [ 1 ] = static_cast < uint8_t > ( value >> 16 );
		out [ 2 ] = static_cast < uint8_t > ( value >> 8 );
		out [ 3 ] = static_cast < uint8_t > ( value );

	}

	static uint32_t be16 ( const uint8_t* in ) {

		return static_cast < uint32_t > ( in [ 0 ] ) << 8 | in [ 1 ];

	}

	static uint32_t be32 ( const uint8_t* in ) {

		return static_cast < uint32_t > ( in [ 0 ] ) << 24 | static_cast < uint32_t > ( in [ 1 ] ) << 16 | static_cast < uint32_t > ( in [ 2 ] ) << 8 | in [ 3 ];

	}

	static void put_text ( uint16_t* words, int32_t first, const char* text, int32_t length ) {

		char padded [ 48 ];
		int32_t used = 0;

		for ( ; text [ used ] && used < length; used++ )
			padded [ used ] = text [ used ];

		for ( ; used < length; used++ )
			padded [ used ] = ' ';

		for ( int32_t i = 0; i < length / 2; i++ )
			words [ first + i ] = static_cast < uint16_t > ( static_cast < uint8_t > ( padded [ i * 2 ] ) << 8 | static_cast < uint8_t > ( padded [ i * 2 + 1 ] ) );

	}

	static void clear ( uint8_t* out, uint32_t size ) {

		for ( uint32_t i = 0; i < size; i++ )
			out [ i ] = 0;

	}

	static void copy_bytes ( uint8_t* out, const uint8_t* in, uint32_t size ) {

		for ( uint32_t i = 0; i < size; i++ )
			out [ i ] = in [ i ];

	}

	static bool media_ready ( const ide_drive& drive ) {

		return drive.media && drive.media->present ( ) && drive.sectors;

	}

	ide_controller::ide_controller ( platform_bus& owner, hv::machine& target ) : bus ( owner ), vm ( target ) {

		for ( ide_channel& channel : channels )
			channel.buffer = static_cast < uint8_t* > ( runtime_allocate_zero ( ide_channel::buffer_size ) );

		identify ( 0x8086, 0x7010, 0x01, 0x01, 0x80, 0x00, 0x00 );
		set_bar ( 4, 16, true, default_master );
		config [ 0x04 ] = 0x05;
		config [ 0x41 ] = 0x80;
		config [ 0x43 ] = 0x80;

	}

	ide_controller::~ide_controller ( ) {

		for ( ide_channel& channel : channels )
			runtime_release ( channel.buffer );

	}

	void ide_controller::reset ( ) {

		for ( int32_t index = 0; index < 2; index++ ) {

			ide_channel& channel = channels [ index ];

			channel.control = 0;
			channel.master_command = 0;
			channel.master_status = 0;
			channel.table = 0;
			soft_reset ( index );
			lower ( index );

		}

	}

	void ide_controller::attach ( int32_t channel_index, int32_t unit, drive_kind kind, disk_file* media ) {

		ide_drive& drive = channels [ channel_index & 1 ].drives [ unit & 1 ];

		drive = ide_drive ( );
		drive.kind = kind;
		change_media ( channel_index, unit, media );
		drive.attention = false;
		drive.media_event = media != nullptr;

	}

	void ide_controller::change_media ( int32_t channel_index, int32_t unit, disk_file* media ) {

		ide_drive& drive = channels [ channel_index & 1 ].drives [ unit & 1 ];

		drive.media = media;
		drive.sectors = 0;

		if ( media && media->present ( ) )
			drive.sectors = drive.kind == DRIVE_OPTICAL ? media->size / optical_block : media->size / 512;

		drive.heads = 16;
		drive.per_track = 63;
		drive.cylinders = static_cast < uint16_t > ( base::min ( drive.sectors / ( 16 * 63 ), static_cast < uint64_t > ( 16383 ) ) );
		drive.attention = drive.kind == DRIVE_OPTICAL;
		drive.media_event = drive.kind == DRIVE_OPTICAL;

	}

	void ide_controller::update_line ( int32_t index ) {

		ide_channel& channel = channels [ index ];
		bool wanted = channel.interrupt && !( channel.control & 0x02 );

		if ( wanted == channel.line )
			return;

		channel.line = wanted;
		bus.set_irq ( 14 + static_cast < uint32_t > ( index ), wanted );

	}

	void ide_controller::raise ( int32_t index ) {

		channels [ index ].interrupt = true;
		channels [ index ].master_status |= 0x04;
		update_line ( index );

	}

	void ide_controller::lower ( int32_t index ) {

		channels [ index ].interrupt = false;
		update_line ( index );

	}

	void ide_controller::signature ( int32_t index, int32_t unit ) {

		ide_channel& channel = channels [ index ];
		bool optical = channel.drives [ unit ].kind == DRIVE_OPTICAL;

		channel.count [ 0 ] = 1;
		channel.lba [ 0 ] [ 0 ] = 1;
		channel.lba [ 1 ] [ 0 ] = optical ? 0x14 : 0x00;
		channel.lba [ 2 ] [ 0 ] = optical ? 0xEB : 0x00;
		channel.device = static_cast < uint8_t > ( 0xA0 | unit << 4 );

	}

	void ide_controller::soft_reset ( int32_t index ) {

		ide_channel& channel = channels [ index ];

		channel.unit = 0;
		signature ( index, 0 );
		channel.error = 1;
		channel.status = channel.drives [ 0 ].kind == DRIVE_NONE ? 0 : static_cast < uint8_t > ( STATUS_READY | STATUS_SEEK );
		channel.transfer = TRANSFER_NONE;
		channel.dma_pending = false;
		channel.position = 0;
		channel.limit = 0;
		channel.filled = 0;
		channel.features = 0;
		channel.count [ 1 ] = 0;

		for ( auto& pair : channel.lba )
			pair [ 1 ] = 0;

		for ( ide_drive& drive : channel.drives ) {

			drive.multiple = 16;
			drive.heads = 16;
			drive.per_track = 63;

		}

	}

	uint64_t ide_controller::read ( uint64_t port, uint32_t size ) {

		if ( port >= 0x1F0 && port <= 0x1F7 )
			return read_register ( 0, static_cast < uint32_t > ( port - 0x1F0 ), size );

		if ( port >= 0x170 && port <= 0x177 )
			return read_register ( 1, static_cast < uint32_t > ( port - 0x170 ), size );

		if ( port == 0x3F6 || port == 0x376 ) {

			ide_channel& channel = channels [ port == 0x376 ? 1 : 0 ];
			bool absent = channel.drives [ channel.unit ].kind == DRIVE_NONE;

			return absent ? ( channel.drives [ channel.unit ^ 1 ].kind == DRIVE_NONE ? 0xFF : 0x00 ) : channel.status;

		}

		if ( port >= master_base && port < master_base + 16u )
			return read_master ( static_cast < uint32_t > ( port - master_base ), size );

		return size >= 4 ? 0xFFFFFFFFu : ( 1u << ( size * 8 ) ) - 1;

	}

	void ide_controller::write ( uint64_t port, uint32_t size, uint64_t value ) {

		uint32_t data = static_cast < uint32_t > ( value );

		if ( port >= 0x1F0 && port <= 0x1F7 )
			write_register ( 0, static_cast < uint32_t > ( port - 0x1F0 ), size, data );
		else if ( port >= 0x170 && port <= 0x177 )
			write_register ( 1, static_cast < uint32_t > ( port - 0x170 ), size, data );
		else if ( port == 0x3F6 || port == 0x376 ) {

			int32_t index = port == 0x376 ? 1 : 0;
			ide_channel& channel = channels [ index ];
			uint8_t byte = static_cast < uint8_t > ( data );
			bool was_reset = ( channel.control & 0x04 ) != 0;

			channel.control = byte;

			if ( byte & 0x04 ) {

				channel.status = STATUS_BUSY;
				channel.transfer = TRANSFER_NONE;

			} else if ( was_reset ) {

				soft_reset ( index );
				lower ( index );

			}

			update_line ( index );

		} else if ( port >= master_base && port < master_base + 16u )
			write_master ( static_cast < uint32_t > ( port - master_base ), size, data );

	}

	void ide_controller::written ( uint32_t offset ) {

		if ( offset > 0x23 || ( offset < 0x20 && offset > 0x05 ) )
			return;

		uint16_t wanted = static_cast < uint16_t > ( bar_address ( 4 ) & 0xFFF0 );

		if ( !wanted || wanted == master_base || wanted > 0xFFF0 )
			return;

		vm.relocate_ports ( this, master_base, wanted, static_cast < uint16_t > ( wanted + 15 ) );
		master_base = wanted;

	}

	uint64_t ide_controller::read_master ( uint32_t offset, uint32_t size ) {

		ide_channel& channel = channels [ offset >> 3 ];
		uint32_t local = offset & 7;
		uint64_t value = 0;

		for ( uint32_t i = 0; i < size && local + i < 8; i++ ) {

			uint32_t at = local + i;
			uint8_t byte = 0;

			if ( at == 0 )
				byte = channel.master_command;
			else if ( at == 2 )
				byte = channel.master_status;
			else if ( at >= 4 )
				byte = static_cast < uint8_t > ( channel.table >> ( ( at - 4 ) * 8 ) );

			value |= static_cast < uint64_t > ( byte ) << ( i * 8 );

		}

		return value;

	}

	void ide_controller::write_master ( uint32_t offset, uint32_t size, uint32_t value ) {

		int32_t index = static_cast < int32_t > ( offset >> 3 );
		ide_channel& channel = channels [ index ];
		uint32_t local = offset & 7;

		for ( uint32_t i = 0; i < size && local + i < 8; i++ ) {

			uint32_t at = local + i;
			uint8_t byte = static_cast < uint8_t > ( value >> ( i * 8 ) );

			if ( at == 0 ) {

				bool starting = ( byte & 1 ) && !( channel.master_command & 1 );

				channel.master_command = static_cast < uint8_t > ( byte & 0x09 );

				if ( byte & 1 ) {

					if ( starting )
						channel.master_status |= 0x01;

				} else
					channel.master_status &= static_cast < uint8_t > ( ~0x01 );

			} else if ( at == 2 )
				channel.master_status = static_cast < uint8_t > ( ( channel.master_status & ~( byte & 0x06 ) & 0x07 ) | ( byte & 0x60 ) );
			else if ( at >= 4 ) {

				uint32_t shift = ( at - 4 ) * 8;

				channel.table = ( channel.table & ~( 0xFFu << shift ) ) | static_cast < uint32_t > ( byte ) << shift;
				channel.table &= ~3u;

			}

		}

		if ( local == 0 && ( channel.master_command & 1 ) )
			try_dma ( index );

	}

	uint64_t ide_controller::read_register ( int32_t index, uint32_t offset, uint32_t size ) {

		ide_channel& channel = channels [ index ];
		ide_drive& drive = selected ( channel );
		bool absent = drive.kind == DRIVE_NONE;
		bool high = ( channel.control & 0x80 ) != 0;

		if ( offset == 0 ) {

			if ( !( channel.status & STATUS_REQUEST ) || ( channel.transfer != TRANSFER_READ && channel.transfer != TRANSFER_REPLY && channel.transfer != TRANSFER_PACKET_IN ) )
				return size >= 4 ? 0xFFFFFFFFu : 0xFFFFu;

			uint64_t value = 0;
			uint32_t taken = 0;

			for ( ; taken < size && channel.position < channel.limit; taken++ )
				value |= static_cast < uint64_t > ( channel.buffer [ channel.position++ ] ) << ( taken * 8 );

			if ( channel.position >= channel.limit )
				data_done ( index );

			return value;

		}

		if ( absent && offset != 7 )
			return channel.drives [ channel.unit ^ 1 ].kind == DRIVE_NONE ? 0xFF : 0x00;

		switch ( offset ) {

			case 1:
				return channel.error;

			case 2:
				return channel.count [ high ? 1 : 0 ];

			case 3:
			case 4:
			case 5:
				return channel.lba [ offset - 3 ] [ high ? 1 : 0 ];

			case 6:
				return channel.device;

			default:

				if ( absent )
					return channel.drives [ channel.unit ^ 1 ].kind == DRIVE_NONE ? 0xFF : 0x00;

				lower ( index );

				return channel.status;

		}

	}

	void ide_controller::write_register ( int32_t index, uint32_t offset, uint32_t size, uint32_t value ) {

		ide_channel& channel = channels [ index ];
		uint8_t byte = static_cast < uint8_t > ( value );

		if ( offset == 0 ) {

			if ( !( channel.status & STATUS_REQUEST ) || ( channel.transfer != TRANSFER_WRITE && channel.transfer != TRANSFER_PACKET && channel.transfer != TRANSFER_PACKET_OUT ) )
				return;

			for ( uint32_t i = 0; i < size && channel.position < channel.limit; i++ )
				channel.buffer [ channel.position++ ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

			if ( channel.position >= channel.limit )
				data_done ( index );

			return;

		}

		if ( offset >= 2 && offset <= 5 )
			channel.control &= static_cast < uint8_t > ( ~0x80 );

		switch ( offset ) {

			case 1:
				channel.features = byte;
				break;

			case 2:
				channel.count [ 1 ] = channel.count [ 0 ];
				channel.count [ 0 ] = byte;
				break;

			case 3:
			case 4:
			case 5:
				channel.lba [ offset - 3 ] [ 1 ] = channel.lba [ offset - 3 ] [ 0 ];
				channel.lba [ offset - 3 ] [ 0 ] = byte;
				break;

			case 6:
				channel.device = static_cast < uint8_t > ( byte | 0xA0 );
				channel.unit = static_cast < uint8_t > ( byte >> 4 & 1 );
				break;

			default:

				if ( selected ( channel ).kind == DRIVE_NONE && byte != 0x90 )
					break;

				lower ( index );
				execute ( index, byte );
				break;

		}

	}

	uint64_t ide_controller::address ( ide_channel& channel, bool extended ) const {

		if ( extended )
			return static_cast < uint64_t > ( channel.lba [ 2 ] [ 1 ] ) << 40 | static_cast < uint64_t > ( channel.lba [ 1 ] [ 1 ] ) << 32 | static_cast < uint64_t > ( channel.lba [ 0 ] [ 1 ] ) << 24 | static_cast < uint64_t > ( channel.lba [ 2 ] [ 0 ] ) << 16 | static_cast < uint64_t > ( channel.lba [ 1 ] [ 0 ] ) << 8 | channel.lba [ 0 ] [ 0 ];

		if ( channel.device & 0x40 )
			return static_cast < uint64_t > ( channel.device & 0x0F ) << 24 | static_cast < uint64_t > ( channel.lba [ 2 ] [ 0 ] ) << 16 | static_cast < uint64_t > ( channel.lba [ 1 ] [ 0 ] ) << 8 | channel.lba [ 0 ] [ 0 ];

		const ide_drive& drive = channel.drives [ channel.unit ];
		uint64_t cylinder = static_cast < uint64_t > ( channel.lba [ 2 ] [ 0 ] ) << 8 | channel.lba [ 1 ] [ 0 ];
		uint64_t head = channel.device & 0x0F;
		uint64_t sector = channel.lba [ 0 ] [ 0 ];

		return ( cylinder * drive.heads + head ) * drive.per_track + ( sector ? sector - 1 : 0 );

	}

	uint32_t ide_controller::sector_count ( ide_channel& channel, bool extended ) const {

		uint32_t value = extended ? static_cast < uint32_t > ( channel.count [ 1 ] ) << 8 | channel.count [ 0 ] : channel.count [ 0 ];

		return value ? value : ( extended ? 65536u : 256u );

	}

	void ide_controller::set_address ( ide_channel& channel, uint64_t value, bool extended ) {

		channel.lba [ 0 ] [ 0 ] = static_cast < uint8_t > ( value );
		channel.lba [ 1 ] [ 0 ] = static_cast < uint8_t > ( value >> 8 );
		channel.lba [ 2 ] [ 0 ] = static_cast < uint8_t > ( value >> 16 );

		if ( extended ) {

			channel.lba [ 0 ] [ 1 ] = static_cast < uint8_t > ( value >> 24 );
			channel.lba [ 1 ] [ 1 ] = static_cast < uint8_t > ( value >> 32 );
			channel.lba [ 2 ] [ 1 ] = static_cast < uint8_t > ( value >> 40 );

		} else if ( channel.device & 0x40 )
			channel.device = static_cast < uint8_t > ( ( channel.device & 0xF0 ) | ( value >> 24 & 0x0F ) );

	}

	void ide_controller::abort_command ( int32_t index ) {

		ide_channel& channel = channels [ index ];

		channel.status = STATUS_READY | STATUS_SEEK | STATUS_ERROR;
		channel.error = ERROR_ABORT;
		channel.transfer = TRANSFER_NONE;
		channel.dma_pending = false;
		raise ( index );

	}

	void ide_controller::finish ( int32_t index, bool interrupt ) {

		ide_channel& channel = channels [ index ];

		channel.status = STATUS_READY | STATUS_SEEK;
		channel.transfer = TRANSFER_NONE;
		channel.dma_pending = false;

		if ( interrupt )
			raise ( index );

	}

	void ide_controller::execute ( int32_t index, uint8_t code ) {

		ide_channel& channel = channels [ index ];
		ide_drive& drive = selected ( channel );
		bool optical = drive.kind == DRIVE_OPTICAL;

		channel.command = code;
		channel.error = 0;
		channel.transfer = TRANSFER_NONE;
		channel.dma_pending = false;

		if ( code == 0x90 ) {

			for ( int32_t unit = 1; unit >= 0; unit-- )
				signature ( index, unit );

			channel.unit = 0;
			channel.error = 1;
			channel.status = channel.drives [ 0 ].kind == DRIVE_NONE ? 0 : static_cast < uint8_t > ( STATUS_READY | STATUS_SEEK );
			raise ( index );

			return;

		}

		if ( optical && code != 0xA0 && code != 0xA1 && code != 0x08 && code != 0xEF && code != 0xE5 && code != 0xE0 && code != 0xE1 && code != 0xE2 && code != 0xE3 && code != 0xE6 && code != 0xE7 && code != 0x00 ) {

			if ( code == 0xEC )
				signature ( index, channel.unit );

			abort_command ( index );

			return;

		}

		switch ( code ) {

			case 0xEC:
				identify_disk ( index );
				return;

			case 0xA1:
				identify_packet ( index );
				return;

			case 0x08:

				if ( !optical ) {

					abort_command ( index );

					return;

				}

				signature ( index, channel.unit );
				channel.status = 0;
				channel.error = 1;
				return;

			case 0xA0:
				channel.byte_limit = static_cast < uint32_t > ( channel.lba [ 2 ] [ 0 ] ) << 8 | channel.lba [ 1 ] [ 0 ];

				if ( !channel.byte_limit || channel.byte_limit == 0xFFFF )
					channel.byte_limit = 0xFFFE;

				channel.byte_limit &= ~1u;
				channel.transfer = TRANSFER_PACKET;
				channel.position = 0;
				channel.limit = 12;
				channel.count [ 0 ] = 1;
				channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;
				return;

			case 0x20:
			case 0x21:
				start_read ( index, address ( channel, false ), sector_count ( channel, false ), 1 );
				return;

			case 0x24:
				start_read ( index, address ( channel, true ), sector_count ( channel, true ), 1 );
				return;

			case 0xC4:
			case 0x29:

				if ( !drive.multiple ) {

					abort_command ( index );

					return;

				}

				start_read ( index, address ( channel, code == 0x29 ), sector_count ( channel, code == 0x29 ), drive.multiple );
				return;

			case 0x30:
			case 0x31:
				start_write ( index, address ( channel, false ), sector_count ( channel, false ), 1 );
				return;

			case 0x34:
				start_write ( index, address ( channel, true ), sector_count ( channel, true ), 1 );
				return;

			case 0xC5:
			case 0x39:

				if ( !drive.multiple ) {

					abort_command ( index );

					return;

				}

				start_write ( index, address ( channel, code == 0x39 ), sector_count ( channel, code == 0x39 ), drive.multiple );
				return;

			case 0xC8:
			case 0xC9:
			case 0x25:
			case 0xCA:
			case 0xCB:
			case 0x35: {

				bool extended = code == 0x25 || code == 0x35;
				bool reading = code == 0xC8 || code == 0xC9 || code == 0x25;
				uint64_t lba = address ( channel, extended );
				uint32_t count = sector_count ( channel, extended );

				if ( !media_ready ( drive ) || lba > drive.sectors || count > drive.sectors - lba || ( !reading && !drive.media->writable ) ) {

					abort_command ( index );

					return;

				}

				channel.next_lba = lba;
				channel.remaining = count;
				channel.unit_size = 512;
				channel.packet_bytes = static_cast < uint64_t > ( count ) * 512;
				channel.position = 0;
				channel.filled = 0;
				channel.stream = true;
				channel.dma_to_memory = reading;
				channel.dma_pending = true;
				channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;
				set_address ( channel, lba + count - 1, extended );
				try_dma ( index );
				return;

			}

			case 0x40:
			case 0x41:
			case 0x42:
			case 0x10:
			case 0x70:
			case 0xE0:
			case 0xE1:
			case 0xE2:
			case 0xE3:
			case 0xE6:
			case 0x94:
			case 0x95:
			case 0x96:
			case 0x97:
			case 0x99:
				finish ( index, true );
				return;

			case 0xE5:
			case 0x98:
				channel.count [ 0 ] = 0xFF;
				finish ( index, true );
				return;

			case 0xC6: {

				uint8_t count = channel.count [ 0 ];

				if ( !count || count > 16 || ( count & ( count - 1 ) ) ) {

					abort_command ( index );

					return;

				}

				drive.multiple = count;
				finish ( index, true );
				return;

			}

			case 0xEF:

				if ( channel.features == 0x03 )
					drive.transfer_mode = channel.count [ 0 ];
				else if ( channel.features == 0x02 || channel.features == 0x82 )
					drive.write_cache = channel.features == 0x02;

				finish ( index, true );
				return;

			case 0x91:
				drive.heads = static_cast < uint16_t > ( ( channel.device & 0x0F ) + 1 );
				drive.per_track = channel.count [ 0 ] ? channel.count [ 0 ] : 63;
				finish ( index, true );
				return;

			case 0xE7:
			case 0xEA:

				if ( drive.media )
					drive.media->flush ( );

				finish ( index, true );
				return;

			case 0xF8:
				set_address ( channel, drive.sectors ? base::min ( drive.sectors, static_cast < uint64_t > ( 0x10000000 ) ) - 1 : 0, false );
				finish ( index, true );
				return;

			case 0x27:
				set_address ( channel, drive.sectors ? drive.sectors - 1 : 0, true );
				finish ( index, true );
				return;

			default:

				if ( code >= 0x11 && code <= 0x1F ) {

					finish ( index, true );

					return;

				}

				abort_command ( index );
				return;

		}

	}

	void ide_controller::start_read ( int32_t index, uint64_t lba, uint32_t count, uint32_t block ) {

		ide_channel& channel = channels [ index ];
		ide_drive& drive = selected ( channel );

		if ( !media_ready ( drive ) || lba > drive.sectors || count > drive.sectors - lba ) {

			abort_command ( index );

			return;

		}

		channel.next_lba = lba;
		channel.remaining = count;
		channel.block = block;
		channel.transfer = TRANSFER_READ;
		fill_block ( index );

	}

	void ide_controller::fill_block ( int32_t index ) {

		ide_channel& channel = channels [ index ];
		ide_drive& drive = selected ( channel );
		uint32_t count = base::min ( channel.remaining, channel.block );
		bool extended = channel.command == 0x24 || channel.command == 0x29;

		if ( !drive.media->read ( channel.next_lba * 512, channel.buffer, count * 512 ) ) {

			channel.status = STATUS_READY | STATUS_ERROR;
			channel.error = 0x40;
			channel.transfer = TRANSFER_NONE;
			raise ( index );

			return;

		}

		InterlockedIncrement ( &busy_light );
		channel.next_lba += count;
		channel.position = 0;
		channel.limit = count * 512;
		channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;
		set_address ( channel, channel.next_lba - 1, extended );
		raise ( index );

	}

	void ide_controller::start_write ( int32_t index, uint64_t lba, uint32_t count, uint32_t block ) {

		ide_channel& channel = channels [ index ];
		ide_drive& drive = selected ( channel );

		if ( !media_ready ( drive ) || !drive.media->writable || lba > drive.sectors || count > drive.sectors - lba ) {

			abort_command ( index );

			return;

		}

		channel.next_lba = lba;
		channel.remaining = count;
		channel.block = block;
		channel.transfer = TRANSFER_WRITE;
		channel.position = 0;
		channel.limit = base::min ( count, block ) * 512;
		channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;

	}

	void ide_controller::block_written ( int32_t index ) {

		ide_channel& channel = channels [ index ];
		ide_drive& drive = selected ( channel );
		uint32_t count = channel.limit / 512;
		bool extended = channel.command == 0x34 || channel.command == 0x39;

		if ( !drive.media->write ( channel.next_lba * 512, channel.buffer, count * 512 ) ) {

			channel.status = STATUS_READY | STATUS_ERROR;
			channel.error = 0x40;
			channel.transfer = TRANSFER_NONE;
			raise ( index );

			return;

		}

		InterlockedIncrement ( &busy_light );
		channel.next_lba += count;
		channel.remaining -= count;
		set_address ( channel, channel.next_lba - 1, extended );

		if ( channel.remaining ) {

			channel.position = 0;
			channel.limit = base::min ( channel.remaining, channel.block ) * 512;
			channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;

		} else
			finish ( index, false );

		raise ( index );

	}

	void ide_controller::identify_disk ( int32_t index ) {

		ide_channel& channel = channels [ index ];
		ide_drive& drive = selected ( channel );
		uint16_t words [ 256 ] = { };
		uint64_t total = drive.sectors;
		uint32_t low = static_cast < uint32_t > ( base::min ( total, static_cast < uint64_t > ( 0x0FFFFFFF ) ) );
		uint32_t chs = static_cast < uint32_t > ( drive.cylinders ) * drive.heads * drive.per_track;
		char serial [ 21 ] = "CB0000000000";

		for ( int32_t i = 0; i < 4; i++ )
			serial [ 8 + i ] = static_cast < char > ( '0' + ( index * 2 + channel.unit + i ) % 10 );

		words [ 0 ] = 0x0040;
		words [ 1 ] = drive.cylinders;
		words [ 3 ] = drive.heads;
		words [ 6 ] = drive.per_track;
		put_text ( words, 10, serial, 20 );
		put_text ( words, 23, "1.0", 8 );
		put_text ( words, 27, "Coldboot Virtual Disk", 40 );
		words [ 47 ] = 0x8010;
		words [ 49 ] = 0x0F00;
		words [ 50 ] = 0x4000;
		words [ 51 ] = 0x0200;
		words [ 52 ] = 0x0200;
		words [ 53 ] = 0x0007;
		words [ 54 ] = drive.cylinders;
		words [ 55 ] = drive.heads;
		words [ 56 ] = drive.per_track;
		words [ 57 ] = static_cast < uint16_t > ( chs );
		words [ 58 ] = static_cast < uint16_t > ( chs >> 16 );
		words [ 59 ] = static_cast < uint16_t > ( drive.multiple ? 0x0100 | drive.multiple : 0 );
		words [ 60 ] = static_cast < uint16_t > ( low );
		words [ 61 ] = static_cast < uint16_t > ( low >> 16 );
		words [ 63 ] = static_cast < uint16_t > ( 0x0007 | ( ( drive.transfer_mode & 0xF8 ) == 0x20 ? 0x100 << ( drive.transfer_mode & 7 ) : 0 ) );
		words [ 64 ] = 0x0003;
		words [ 65 ] = 120;
		words [ 66 ] = 120;
		words [ 67 ] = 120;
		words [ 68 ] = 120;
		words [ 80 ] = 0x00F0;
		words [ 82 ] = 0x0068;
		words [ 83 ] = 0x7400;
		words [ 84 ] = 0x4000;
		words [ 85 ] = static_cast < uint16_t > ( drive.write_cache ? 0x0068 : 0x0048 );
		words [ 86 ] = 0x3400;
		words [ 87 ] = 0x4000;
		words [ 88 ] = static_cast < uint16_t > ( 0x003F | ( ( drive.transfer_mode & 0xF8 ) == 0x40 ? 0x100 << ( drive.transfer_mode & 7 ) : 0 ) );
		words [ 93 ] = 0x6001;
		words [ 100 ] = static_cast < uint16_t > ( total );
		words [ 101 ] = static_cast < uint16_t > ( total >> 16 );
		words [ 102 ] = static_cast < uint16_t > ( total >> 32 );
		words [ 103 ] = static_cast < uint16_t > ( total >> 48 );
		words [ 106 ] = 0x4000;
		words [ 217 ] = 0x0001;

		uint8_t sum = 0xA5;

		for ( int32_t i = 0; i < 255; i++ )
			sum = static_cast < uint8_t > ( sum + ( words [ i ] & 0xFF ) + ( words [ i ] >> 8 ) );

		words [ 255 ] = static_cast < uint16_t > ( 0xA5 | static_cast < uint16_t > ( static_cast < uint8_t > ( -sum ) ) << 8 );

		for ( int32_t i = 0; i < 256; i++ ) {

			channel.buffer [ i * 2 ] = static_cast < uint8_t > ( words [ i ] );
			channel.buffer [ i * 2 + 1 ] = static_cast < uint8_t > ( words [ i ] >> 8 );

		}

		channel.position = 0;
		channel.limit = 512;
		channel.transfer = TRANSFER_REPLY;
		channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;
		raise ( index );

	}

	void ide_controller::identify_packet ( int32_t index ) {

		ide_channel& channel = channels [ index ];
		uint16_t words [ 256 ] = { };

		words [ 0 ] = 0x85C0;
		put_text ( words, 10, "CB-DVD-0001", 20 );
		put_text ( words, 23, "1.0", 8 );
		put_text ( words, 27, "Coldboot Virtual DVD", 40 );
		words [ 49 ] = 0x0F00;
		words [ 53 ] = 0x0007;
		words [ 62 ] = 0x0007;
		words [ 63 ] = 0x0007;
		words [ 64 ] = 0x0003;
		words [ 65 ] = 180;
		words [ 66 ] = 180;
		words [ 67 ] = 180;
		words [ 68 ] = 180;
		words [ 71 ] = 30;
		words [ 72 ] = 30;
		words [ 80 ] = 0x001E;
		words [ 82 ] = 0x0010;
		words [ 83 ] = 0x4000;
		words [ 84 ] = 0x4000;
		words [ 85 ] = 0x0010;
		words [ 87 ] = 0x4000;
		words [ 88 ] = 0x003F;
		words [ 93 ] = 0x6001;

		for ( int32_t i = 0; i < 256; i++ ) {

			channel.buffer [ i * 2 ] = static_cast < uint8_t > ( words [ i ] );
			channel.buffer [ i * 2 + 1 ] = static_cast < uint8_t > ( words [ i ] >> 8 );

		}

		channel.position = 0;
		channel.limit = 512;
		channel.transfer = TRANSFER_REPLY;
		channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;
		raise ( index );

	}

	void ide_controller::data_done ( int32_t index ) {

		ide_channel& channel = channels [ index ];

		switch ( channel.transfer ) {

			case TRANSFER_READ:
				channel.remaining -= channel.limit / 512;

				if ( channel.remaining )
					fill_block ( index );
				else
					finish ( index, false );

				return;

			case TRANSFER_WRITE:
				block_written ( index );
				return;

			case TRANSFER_REPLY:
				finish ( index, false );
				return;

			case TRANSFER_PACKET:
				packet_command ( index );
				return;

			case TRANSFER_PACKET_IN:
				channel.packet_bytes -= channel.block;

				if ( channel.packet_bytes )
					packet_chunk ( index );
				else
					packet_good ( index );

				return;

			case TRANSFER_PACKET_OUT:
				channel.packet_bytes -= channel.block;

				if ( channel.packet_bytes ) {

					channel.block = static_cast < uint32_t > ( base::min ( static_cast < uint64_t > ( base::min ( channel.byte_limit, ide_channel::buffer_size ) ), channel.packet_bytes ) );
					channel.position = 0;
					channel.limit = channel.block;
					raise ( index );

				} else
					packet_good ( index );

				return;

			default:
				return;

		}

	}

	void ide_controller::packet_good ( int32_t index ) {

		ide_channel& channel = channels [ index ];

		channel.status = STATUS_READY | STATUS_SEEK;
		channel.error = 0;
		channel.count [ 0 ] = 3;
		channel.transfer = TRANSFER_NONE;
		channel.dma_pending = false;
		raise ( index );

	}

	void ide_controller::packet_error ( int32_t index, uint8_t key, uint8_t code, uint8_t qualifier ) {

		ide_channel& channel = channels [ index ];
		ide_drive& drive = selected ( channel );

		drive.sense_key = key;
		drive.sense_code = code;
		drive.sense_qualifier = qualifier;
		channel.status = STATUS_READY | STATUS_SEEK | STATUS_ERROR;
		channel.error = static_cast < uint8_t > ( key << 4 | ERROR_ABORT );
		channel.count [ 0 ] = 3;
		channel.transfer = TRANSFER_NONE;
		channel.dma_pending = false;
		raise ( index );

	}

	void ide_controller::packet_reply ( int32_t index, uint32_t length, uint32_t allowed ) {

		ide_channel& channel = channels [ index ];
		uint32_t count = base::min ( length, allowed );

		channel.stream = false;
		channel.remaining = 0;
		channel.position = 0;
		channel.filled = count;
		channel.packet_bytes = count;

		if ( !count ) {

			packet_good ( index );

			return;

		}

		if ( channel.features & 1 ) {

			channel.dma_to_memory = true;
			channel.dma_pending = true;
			channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;
			try_dma ( index );

			return;

		}

		packet_chunk ( index );

	}

	void ide_controller::packet_stream ( int32_t index, uint64_t lba, uint32_t blocks ) {

		ide_channel& channel = channels [ index ];

		channel.stream = true;
		channel.unit_size = optical_block;
		channel.next_lba = lba;
		channel.remaining = blocks;
		channel.position = 0;
		channel.filled = 0;
		channel.packet_bytes = static_cast < uint64_t > ( blocks ) * optical_block;

		if ( !blocks ) {

			packet_good ( index );

			return;

		}

		if ( channel.features & 1 ) {

			channel.dma_to_memory = true;
			channel.dma_pending = true;
			channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;
			try_dma ( index );

			return;

		}

		packet_chunk ( index );

	}

	void ide_controller::packet_chunk ( int32_t index ) {

		ide_channel& channel = channels [ index ];

		if ( channel.position >= channel.filled && channel.stream ) {

			uint32_t blocks = base::min ( channel.remaining, ide_channel::buffer_size / optical_block );
			ide_drive& drive = selected ( channel );

			if ( !blocks || !drive.media || !drive.media->read ( channel.next_lba * optical_block, channel.buffer, blocks * optical_block ) ) {

				packet_error ( index, 0x03, 0x11, 0x00 );

				return;

			}

			InterlockedIncrement ( &busy_light );
			channel.next_lba += blocks;
			channel.remaining -= blocks;
			channel.filled = blocks * optical_block;
			channel.position = 0;

		}

		uint32_t limit = channel.byte_limit;

		if ( channel.stream && limit >= optical_block )
			limit -= limit % optical_block;

		uint32_t chunk = static_cast < uint32_t > ( base::min ( static_cast < uint64_t > ( base::min ( limit, channel.filled - channel.position ) ), channel.packet_bytes ) );

		channel.block = chunk;
		channel.limit = channel.position + chunk;
		channel.lba [ 1 ] [ 0 ] = static_cast < uint8_t > ( chunk );
		channel.lba [ 2 ] [ 0 ] = static_cast < uint8_t > ( chunk >> 8 );
		channel.count [ 0 ] = 2;
		channel.transfer = TRANSFER_PACKET_IN;
		channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;
		raise ( index );

	}

	uint32_t ide_controller::produce ( ide_channel& channel, uint8_t* out, uint32_t room ) {

		uint32_t done = 0;

		while ( done < room && channel.packet_bytes ) {

			if ( channel.position >= channel.filled ) {

				if ( !channel.stream || !channel.remaining )
					break;

				ide_drive& drive = selected ( channel );
				uint32_t units = base::min ( channel.remaining, ide_channel::buffer_size / channel.unit_size );

				if ( !drive.media || !drive.media->read ( channel.next_lba * channel.unit_size, channel.buffer, units * channel.unit_size ) )
					break;

				InterlockedIncrement ( &busy_light );
				channel.next_lba += units;
				channel.remaining -= units;
				channel.filled = units * channel.unit_size;
				channel.position = 0;

			}

			uint32_t take = static_cast < uint32_t > ( base::min ( static_cast < uint64_t > ( base::min ( room - done, channel.filled - channel.position ) ), channel.packet_bytes ) );

			copy_bytes ( out + done, channel.buffer + channel.position, take );
			channel.position += take;
			channel.packet_bytes -= take;
			done += take;

		}

		return done;

	}

	void ide_controller::consume ( ide_channel& channel, const uint8_t* data, uint32_t size ) {

		ide_drive& drive = selected ( channel );
		uint32_t used = 0;

		while ( used < size && channel.remaining ) {

			uint32_t wanted = base::min ( channel.remaining * 512, ide_channel::buffer_size );
			uint32_t take = base::min ( size - used, wanted - channel.position );

			copy_bytes ( channel.buffer + channel.position, data + used, take );
			channel.position += take;
			used += take;

			if ( channel.position >= wanted ) {

				uint32_t units = wanted / 512;

				if ( drive.media )
					drive.media->write ( channel.next_lba * 512, channel.buffer, wanted );

				InterlockedIncrement ( &busy_light );
				channel.next_lba += units;
				channel.remaining -= units;
				channel.packet_bytes = static_cast < uint64_t > ( channel.remaining ) * 512;
				channel.position = 0;

			}

		}

	}

	void ide_controller::try_dma ( int32_t index ) {

		ide_channel& channel = channels [ index ];

		if ( !channel.dma_pending || !( channel.master_command & 1 ) )
			return;

		bool optical = selected ( channel ).kind == DRIVE_OPTICAL;
		uint32_t entry = channel.table;
		uint8_t* scratch = static_cast < uint8_t* > ( runtime_allocate ( 0x10000 ) );

		for ( uint32_t guard = 0; guard < 4096 && channel.packet_bytes; guard++ ) {

			uint8_t descriptor [ 8 ];

			vm.read ( entry, descriptor, 8 );

			uint32_t where = ( static_cast < uint32_t > ( descriptor [ 0 ] ) | static_cast < uint32_t > ( descriptor [ 1 ] ) << 8 | static_cast < uint32_t > ( descriptor [ 2 ] ) << 16 | static_cast < uint32_t > ( descriptor [ 3 ] ) << 24 ) & ~1u;
			uint32_t length = static_cast < uint32_t > ( descriptor [ 4 ] ) | static_cast < uint32_t > ( descriptor [ 5 ] ) << 8;
			bool last = ( descriptor [ 7 ] & 0x80 ) != 0;

			if ( !length )
				length = 0x10000;

			if ( channel.dma_to_memory ) {

				uint8_t* direct = vm.core.host ( where, length );
				uint32_t made = produce ( channel, direct ? direct : scratch, length );

				if ( !direct )
					vm.write ( where, scratch, made );

			} else {

				uint32_t take = static_cast < uint32_t > ( base::min ( static_cast < uint64_t > ( length ), channel.packet_bytes ) );
				const uint8_t* direct = vm.core.host ( where, take );

				if ( !direct )
					vm.read ( where, scratch, take );

				consume ( channel, direct ? direct : scratch, take );

			}

			entry += 8;

			if ( last )
				break;

		}

		runtime_release ( scratch );
		channel.master_status = static_cast < uint8_t > ( ( channel.master_status & ~0x01 ) | 0x04 );
		channel.dma_pending = false;

		if ( optical )
			packet_good ( index );
		else
			finish ( index, true );

	}

	uint32_t ide_controller::mode_page ( uint8_t page, uint8_t* out ) {

		if ( page == 0x01 ) {

			clear ( out, 12 );
			out [ 0 ] = 0x01;
			out [ 1 ] = 0x0A;
			out [ 3 ] = 0x05;

			return 12;

		}

		if ( page == 0x0D ) {

			clear ( out, 8 );
			out [ 0 ] = 0x0D;
			out [ 1 ] = 0x06;
			out [ 5 ] = 0x3C;
			out [ 7 ] = 0x4B;

			return 8;

		}

		if ( page == 0x1A ) {

			clear ( out, 12 );
			out [ 0 ] = 0x1A;
			out [ 1 ] = 0x0A;

			return 12;

		}

		if ( page == 0x2A ) {

			clear ( out, 28 );
			out [ 0 ] = 0x2A;
			out [ 1 ] = 0x1A;
			out [ 2 ] = 0x3B;
			out [ 4 ] = 0x71;
			out [ 5 ] = 0x60;
			out [ 6 ] = 0x29;
			put_be16 ( out + 8, 5540 );
			put_be16 ( out + 10, 2 );
			put_be16 ( out + 12, 512 );
			put_be16 ( out + 14, 5540 );

			return 28;

		}

		return 0;

	}

	uint32_t ide_controller::table_of_contents ( ide_drive& drive, const uint8_t* packet, uint8_t* out ) {

		bool msf = ( packet [ 1 ] & 0x02 ) != 0;
		uint32_t format = packet [ 2 ] & 0x0F;

		if ( !format && ( packet [ 9 ] >> 6 ) )
			format = packet [ 9 ] >> 6;

		uint32_t total = static_cast < uint32_t > ( drive.sectors );

		auto put_address = [ & ] ( uint8_t* at, uint32_t lba ) {

			if ( msf ) {

				uint32_t frames = lba + 150;

				at [ 0 ] = 0;
				at [ 1 ] = static_cast < uint8_t > ( frames / ( 75 * 60 ) );
				at [ 2 ] = static_cast < uint8_t > ( frames / 75 % 60 );
				at [ 3 ] = static_cast < uint8_t > ( frames % 75 );

			} else
				put_be32 ( at, lba );

		};

		if ( format == 0 ) {

			uint32_t used = 4;
			uint32_t start = packet [ 6 ];

			if ( start > 1 && start != 0xAA )
				return 0;

			if ( start <= 1 ) {

				clear ( out + used, 8 );
				out [ used + 1 ] = 0x14;
				out [ used + 2 ] = 1;
				put_address ( out + used + 4, 0 );
				used += 8;

			}

			clear ( out + used, 8 );
			out [ used + 1 ] = 0x14;
			out [ used + 2 ] = 0xAA;
			put_address ( out + used + 4, total );
			used += 8;
			put_be16 ( out, used - 2 );
			out [ 2 ] = 1;
			out [ 3 ] = 1;

			return used;

		}

		if ( format == 1 ) {

			clear ( out, 12 );
			put_be16 ( out, 10 );
			out [ 2 ] = 1;
			out [ 3 ] = 1;
			out [ 5 ] = 0x14;
			out [ 6 ] = 1;
			put_address ( out + 8, 0 );

			return 12;

		}

		return 0;

	}

	void ide_controller::packet_command ( int32_t index ) {

		ide_channel& channel = channels [ index ];
		ide_drive& drive = selected ( channel );
		const uint8_t* packet = channel.buffer;
		uint8_t* out = channel.buffer;
		uint8_t code = packet [ 0 ];
		bool ready = media_ready ( drive );
		bool dvd = drive.media && drive.media->size >= dvd_threshold;

		copy_bytes ( channel.packet, packet, 12 );
		packet = channel.packet;

		if ( drive.attention && code != 0x12 && code != 0x03 && code != 0x46 && code != 0x4A ) {

			drive.attention = false;
			packet_error ( index, 0x06, 0x28, 0x00 );

			return;

		}

		auto needs_media = [ & ] ( ) {

			if ( ready )
				return true;

			packet_error ( index, 0x02, 0x3A, 0x00 );

			return false;

		};

		switch ( code ) {

			case 0x00:

				if ( needs_media ( ) )
					packet_good ( index );

				return;

			case 0x03: {

				clear ( out, 18 );
				out [ 0 ] = 0x70;
				out [ 2 ] = drive.sense_key;
				out [ 7 ] = 10;
				out [ 12 ] = drive.sense_code;
				out [ 13 ] = drive.sense_qualifier;
				drive.sense_key = 0;
				drive.sense_code = 0;
				drive.sense_qualifier = 0;
				packet_reply ( index, 18, packet [ 4 ] );

				return;

			}

			case 0x12: {

				if ( packet [ 1 ] & 1 ) {

					packet_error ( index, 0x05, 0x24, 0x00 );

					return;

				}

				clear ( out, 36 );
				out [ 0 ] = 0x05;
				out [ 1 ] = 0x80;
				out [ 2 ] = 0x00;
				out [ 3 ] = 0x21;
				out [ 4 ] = 31;

				const char* vendor = "COLDBOOTDVD-ROM         1.0 ";

				for ( int32_t i = 0; i < 28; i++ )
					out [ 8 + i ] = static_cast < uint8_t > ( vendor [ i ] );

				packet_reply ( index, 36, be16 ( packet + 3 ) );

				return;

			}

			case 0x1A:
			case 0x5A: {

				bool wide = code == 0x5A;
				uint8_t page = packet [ 2 ] & 0x3F;
				uint32_t header = wide ? 8 : 4;
				uint32_t used = header;

				if ( packet [ 2 ] >> 6 == 3 ) {

					packet_error ( index, 0x05, 0x39, 0x00 );

					return;

				}

				if ( page == 0x3F ) {

					static const uint8_t pages [ ] = { 0x01, 0x0D, 0x1A, 0x2A };

					for ( uint8_t each : pages )
						used += mode_page ( each, out + used );

				} else {

					uint32_t size = mode_page ( page, out + used );

					if ( !size ) {

						packet_error ( index, 0x05, 0x24, 0x00 );

						return;

					}

					used += size;

				}

				clear ( out, header );

				if ( wide )
					put_be16 ( out, used - 2 );
				else
					out [ 0 ] = static_cast < uint8_t > ( used - 1 );

				packet_reply ( index, used, wide ? be16 ( packet + 7 ) : packet [ 4 ] );

				return;

			}

			case 0x1B:
			case 0x2B:
			case 0xBB:
				packet_good ( index );
				return;

			case 0x1E:
				drive.locked = ( packet [ 4 ] & 1 ) != 0;
				packet_good ( index );
				return;

			case 0x25:

				if ( !needs_media ( ) )
					return;

				put_be32 ( out, static_cast < uint32_t > ( drive.sectors - 1 ) );
				put_be32 ( out + 4, optical_block );
				packet_reply ( index, 8, 8 );
				return;

			case 0x28:
			case 0xA8:
			case 0xBE: {

				if ( !needs_media ( ) )
					return;

				uint64_t lba = be32 ( packet + 2 );
				uint32_t blocks = code == 0x28 ? be16 ( packet + 7 ) : ( code == 0xA8 ? be32 ( packet + 6 ) : ( static_cast < uint32_t > ( packet [ 6 ] ) << 16 | be16 ( packet + 7 ) ) );

				if ( code == 0xBE && ( packet [ 9 ] & 0xF8 ) != 0x10 ) {

					packet_error ( index, 0x05, 0x24, 0x00 );

					return;

				}

				if ( lba > drive.sectors || blocks > drive.sectors - lba ) {

					packet_error ( index, 0x05, 0x21, 0x00 );

					return;

				}

				packet_stream ( index, lba, blocks );
				return;

			}

			case 0x43: {

				if ( !needs_media ( ) )
					return;

				uint32_t size = table_of_contents ( drive, packet, out );

				if ( !size ) {

					packet_error ( index, 0x05, 0x24, 0x00 );

					return;

				}

				packet_reply ( index, size, be16 ( packet + 7 ) );
				return;

			}

			case 0x46: {

				uint32_t profile = ready ? ( dvd ? 0x0010u : 0x0008u ) : 0u;
				uint32_t start = be16 ( packet + 2 );
				uint32_t request = packet [ 1 ] & 3;
				uint32_t used = 8;

				clear ( out, 64 );

				auto wanted = [ & ] ( uint32_t feature ) {

					return request == 2 ? feature == start : feature >= start;

				};

				if ( wanted ( 0x0000 ) ) {

					out [ used + 2 ] = 0x03;
					out [ used + 3 ] = 8;
					put_be16 ( out + used + 4, 0x0010 );
					out [ used + 6 ] = profile == 0x0010 ? 1 : 0;
					put_be16 ( out + used + 8, 0x0008 );
					out [ used + 10 ] = profile == 0x0008 ? 1 : 0;
					used += 12;

				}

				if ( wanted ( 0x0001 ) ) {

					put_be16 ( out + used, 0x0001 );
					out [ used + 2 ] = 0x0B;
					out [ used + 3 ] = 8;
					put_be32 ( out + used + 4, 2 );
					out [ used + 8 ] = 1;
					used += 12;

				}

				if ( wanted ( 0x0003 ) ) {

					put_be16 ( out + used, 0x0003 );
					out [ used + 2 ] = 0x03;
					out [ used + 3 ] = 4;
					out [ used + 4 ] = 0x29;
					used += 8;

				}

				if ( wanted ( 0x0010 ) ) {

					put_be16 ( out + used, 0x0010 );
					out [ used + 2 ] = static_cast < uint8_t > ( ready ? 0x01 : 0x00 );
					out [ used + 3 ] = 8;
					put_be32 ( out + used + 4, optical_block );
					put_be16 ( out + used + 8, dvd ? 16 : 1 );
					used += 12;

				}

				put_be32 ( out, used - 4 );
				put_be16 ( out + 6, profile );
				packet_reply ( index, used, be16 ( packet + 7 ) );

				return;

			}

			case 0x4A: {

				if ( !( packet [ 1 ] & 1 ) ) {

					packet_error ( index, 0x05, 0x24, 0x00 );

					return;

				}

				clear ( out, 8 );

				if ( packet [ 4 ] & 0x10 ) {

					put_be16 ( out, 6 );
					out [ 2 ] = 0x04;
					out [ 3 ] = 0x10;
					out [ 4 ] = drive.media_event ? 0x02 : 0x00;
					out [ 5 ] = ready ? 0x02 : 0x00;
					drive.media_event = false;
					packet_reply ( index, 8, be16 ( packet + 7 ) );

				} else {

					put_be16 ( out, 2 );
					out [ 2 ] = 0x80;
					out [ 3 ] = 0x10;
					packet_reply ( index, 4, be16 ( packet + 7 ) );

				}

				return;

			}

			case 0x51: {

				if ( !needs_media ( ) )
					return;

				clear ( out, 34 );
				put_be16 ( out, 32 );
				out [ 2 ] = 0x0E;
				out [ 3 ] = 1;
				out [ 4 ] = 1;
				out [ 5 ] = 1;
				out [ 6 ] = 1;
				packet_reply ( index, 34, be16 ( packet + 7 ) );

				return;

			}

			case 0x52: {

				if ( !needs_media ( ) )
					return;

				clear ( out, 36 );
				put_be16 ( out, 34 );
				out [ 2 ] = 1;
				out [ 3 ] = 1;
				out [ 5 ] = 0x04;
				out [ 6 ] = 0x01;
				put_be32 ( out + 24, static_cast < uint32_t > ( drive.sectors ) );
				packet_reply ( index, 36, be16 ( packet + 7 ) );

				return;

			}

			case 0xAD: {

				if ( !needs_media ( ) )
					return;

				if ( !dvd ) {

					packet_error ( index, 0x05, 0x30, 0x02 );

					return;

				}

				uint8_t format = packet [ 7 ];
				uint32_t size = 0;

				clear ( out, 2052 );

				if ( format == 0x00 ) {

					put_be16 ( out, 2050 );
					out [ 4 ] = 0x01;
					out [ 5 ] = 0x0F;
					out [ 6 ] = 0x01;
					put_be32 ( out + 8, 0x30000 );
					put_be32 ( out + 12, static_cast < uint32_t > ( drive.sectors + 0x30000 - 1 ) );
					size = 2052;

				} else if ( format == 0x01 ) {

					put_be16 ( out, 6 );
					size = 8;

				} else if ( format == 0x04 ) {

					put_be16 ( out, 2050 );
					size = 2052;

				} else {

					packet_error ( index, 0x05, 0x24, 0x00 );

					return;

				}

				packet_reply ( index, size, be16 ( packet + 8 ) );

				return;

			}

			case 0xBD:
				clear ( out, 8 );
				put_be16 ( out + 6, 0 );
				packet_reply ( index, 8, be16 ( packet + 8 ) );
				return;

			case 0x15:
			case 0x55: {

				uint32_t length = code == 0x15 ? packet [ 4 ] : be16 ( packet + 7 );

				if ( !length ) {

					packet_good ( index );

					return;

				}

				channel.packet_bytes = length;
				channel.block = base::min ( base::min ( channel.byte_limit, ide_channel::buffer_size ), length );
				channel.position = 0;
				channel.limit = channel.block;
				channel.lba [ 1 ] [ 0 ] = static_cast < uint8_t > ( channel.block );
				channel.lba [ 2 ] [ 0 ] = static_cast < uint8_t > ( channel.block >> 8 );
				channel.count [ 0 ] = 0;
				channel.transfer = TRANSFER_PACKET_OUT;
				channel.status = STATUS_READY | STATUS_SEEK | STATUS_REQUEST;
				raise ( index );

				return;

			}

			default:
				packet_error ( index, 0x05, 0x20, 0x00 );
				return;

		}

	}

}