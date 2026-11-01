#include "storage/fdc.hh"

namespace machine {

	static uint8_t command_length ( uint8_t code ) {

		switch ( code & 0x1F ) {

			case 0x02:
			case 0x05:
			case 0x06:
			case 0x09:
			case 0x0C:
				return 9;

			case 0x0D:
				return 6;

			case 0x03:
			case 0x0F:
				return 3;

			case 0x04:
			case 0x07:
			case 0x0A:
				return 2;

			default:
				return 1;

		}

	}

	fdc::fdc ( dma& transfers, bus& memory ) : transfers ( transfers ), memory ( memory ) {

		drives [ 0 ].attached = true;
		drives [ 1 ].attached = true;

	}

	void fdc::reset ( ) {

		output = 0;
		step_rate = 0x0C;
		clear_controller ( );

		for ( floppy_drive& drive : drives )
			drive.cylinder = 0;

	}

	void fdc::clear_controller ( ) {

		phase = FDC_COMMAND;
		task = TASK_NONE;
		place = nullptr;
		wait = 0;
		offset = 0;
		received = 0;
		needed = 0;
		result_size = 0;
		result_index = 0;
		reset_reports = 0;
		terminal = false;
		stalled = false;
		pending = false;
		result_clears = false;

		for ( floppy_drive& drive : drives ) {

			drive.seeking = false;
			drive.reported = false;

		}

	}

	bool fdc::insert ( int32_t index, const uint8_t* data, uint32_t size, bool protect ) {

		if ( index < 0 || index >= drive_count || !drives [ index ].attached || !drives [ index ].disk.load ( data, size ) )
			return false;

		drives [ index ].disk.protect = protect;

		return true;

	}

	void fdc::eject ( int32_t index ) {

		if ( index >= 0 && index < drive_count )
			drives [ index ].disk.clear ( );

	}

	void fdc::advance ( uint32_t cycles ) {

		for ( int32_t i = 0; i < drive_count; i++ ) {

			floppy_drive& drive = drives [ i ];

			if ( !drive.seeking )
				continue;

			if ( drive.seek_wait > cycles ) {

				drive.seek_wait -= cycles;

				continue;

			}

			uint8_t status = static_cast < uint8_t > ( 0x20 | drive.side << 2 | i );

			if ( drive.attached )
				drive.cylinder = drive.target;
			else if ( drive.calibrating )
				status = static_cast < uint8_t > ( status | 0x50 );

			drive.status = status;
			drive.seeking = false;
			drive.reported = true;
			pending = true;

		}

		if ( phase != FDC_EXECUTION || stalled )
			return;

		while ( true ) {

			if ( wait > cycles ) {

				wait -= cycles;

				return;

			}

			cycles -= wait;
			wait = byte_cycles;
			work ( );

			if ( phase != FDC_EXECUTION || stalled )
				return;

		}

	}

	uint8_t fdc::in ( uint16_t port ) {

		switch ( port & 7 ) {

			case 4:
				return main_status ( );

			case 5:
				return read_data ( );

			default:
				return 0xFF;

		}

	}

	void fdc::out ( uint16_t port, uint8_t value ) {

		switch ( port & 7 ) {

			case 2:
				write_output ( value );
				return;

			case 5:
				write_data ( value );
				return;

			default:
				return;

		}

	}

	void fdc::write_output ( uint8_t value ) {

		bool was_reset = !( output & 0x04 );

		output = value;

		if ( !( value & 0x04 ) ) {

			clear_controller ( );

			return;

		}

		if ( was_reset ) {

			reset_reports = 4;
			pending = true;

		}

	}

	uint8_t fdc::main_status ( ) const {

		if ( !( output & 0x04 ) )
			return 0;

		uint8_t value = 0;

		for ( int32_t i = 0; i < drive_count; i++ ) {

			if ( drives [ i ].seeking )
				value = static_cast < uint8_t > ( value | 1 << i );

		}

		switch ( phase ) {

			case FDC_COMMAND:
				return static_cast < uint8_t > ( value | 0x80 | ( received ? 0x10 : 0 ) );

			case FDC_EXECUTION:
				return static_cast < uint8_t > ( value | 0x10 );

			default:
				return static_cast < uint8_t > ( value | 0xD0 );

		}

	}

	uint8_t fdc::read_data ( ) {

		if ( phase != FDC_RESULT )
			return 0xFF;

		if ( !result_index && result_clears )
			pending = false;

		uint8_t value = result [ result_index++ ];

		if ( result_index >= result_size ) {

			phase = FDC_COMMAND;
			result_size = 0;
			result_index = 0;

		}

		return value;

	}

	void fdc::write_data ( uint8_t value ) {

		if ( !( output & 0x04 ) || phase != FDC_COMMAND )
			return;

		if ( !received ) {

			needed = command_length ( value );

			if ( ( value & 0x1F ) != 0x08 )
				reset_reports = 0;

		}

		command [ received++ ] = value;

		if ( received < needed )
			return;

		received = 0;
		execute ( );

	}

	void fdc::respond ( uint8_t count, bool clears ) {

		result_size = count;
		result_index = 0;
		result_clears = clears;
		phase = FDC_RESULT;

	}

	void fdc::execute ( ) {

		uint8_t code = static_cast < uint8_t > ( command [ 0 ] & 0x1F );

		switch ( code ) {

			case 0x03:
				step_rate = static_cast < uint8_t > ( command [ 1 ] >> 4 );
				return;

			case 0x08:
				sense ( );
				return;

			default:
				break;

		}

		unit = static_cast < uint8_t > ( command [ 1 ] & 3 );
		head = static_cast < uint8_t > ( command [ 1 ] >> 2 & 1 );

		switch ( code ) {

			case 0x04: {

				floppy_drive& drive = drives [ unit ];
				bool protect = !drive.disk.present ( ) || drive.disk.protect;
				uint8_t status = static_cast < uint8_t > ( unit | head << 2 );

				if ( drive.attached )
					status = static_cast < uint8_t > ( status | 0x28 | ( drive.cylinder ? 0 : 0x10 ) | ( protect ? 0x40 : 0 ) );

				result [ 0 ] = status;
				respond ( 1, false );

				return;

			}

			case 0x07:
				seek ( unit, 0, true );
				return;

			case 0x0F:
				seek ( unit, command [ 2 ], false );
				return;

			case 0x0A:
				start ( TASK_IDENTIFY );
				return;

			case 0x02:
			case 0x06:
			case 0x0C:
				start ( TASK_READ );
				return;

			case 0x05:
			case 0x09:
				start ( TASK_WRITE );
				return;

			case 0x0D:
				start ( TASK_FORMAT );
				return;

			default:
				result [ 0 ] = 0x80;
				respond ( 1, false );
				return;

		}

	}

	void fdc::seek ( uint8_t index, uint8_t cylinder, bool calibrate ) {

		floppy_drive& drive = drives [ index ];
		uint8_t target = base::min ( cylinder, static_cast < uint8_t > ( 83 ) );
		uint32_t steps = drive.cylinder > target ? drive.cylinder - target : target - drive.cylinder;
		uint32_t step = ( 16u - step_rate ) * 2u * millisecond;

		if ( calibrate && !drive.attached )
			steps = 77;

		drive.target = target;
		drive.side = head;
		drive.calibrating = calibrate;
		drive.seek_wait = base::max ( steps, 1u ) * step;
		drive.seeking = true;
		drive.reported = false;

	}

	void fdc::sense ( ) {

		for ( floppy_drive& drive : drives ) {

			if ( !drive.reported )
				continue;

			drive.reported = false;
			result [ 0 ] = drive.status;
			result [ 1 ] = drive.cylinder;
			pending = false;
			respond ( 2, false );

			return;

		}

		if ( reset_reports ) {

			uint8_t index = static_cast < uint8_t > ( drive_count - reset_reports );

			reset_reports--;
			result [ 0 ] = static_cast < uint8_t > ( 0xC0 | index );
			result [ 1 ] = drives [ index ].cylinder;
			pending = false;
			respond ( 2, false );

			return;

		}

		result [ 0 ] = 0x80;
		respond ( 1, false );

	}

	void fdc::start ( fdc_task kind ) {

		task = kind;
		phase = FDC_EXECUTION;
		wait = settle_cycles;
		place = nullptr;
		offset = 0;
		terminal = false;
		stalled = false;
		multi_track = ( command [ 0 ] & 0x80 ) && ( kind == TASK_READ || kind == TASK_WRITE );

		if ( kind == TASK_READ || kind == TASK_WRITE ) {

			for ( int32_t i = 0; i < 4; i++ )
				id [ i ] = command [ 2 + i ];

			last_record = command [ 6 ];

			if ( ( command [ 0 ] & 0x1F ) == 0x02 )
				id [ 2 ] = 1;

		} else if ( kind == TASK_FORMAT ) {

			format_count = command [ 3 ];
			filler = command [ 5 ];
			formatted = 0;
			field = 0;

		}

	}

	void fdc::work ( ) {

		floppy_drive& drive = drives [ unit ];

		if ( !drive.attached || !drive.disk.present ( ) ) {

			stalled = true;

			return;

		}

		switch ( task ) {

			case TASK_IDENTIFY:
				identify ( drive );
				return;

			case TASK_FORMAT:
				format_step ( drive );
				return;

			default:
				move_data ( drive );
				return;

		}

	}

	void fdc::identify ( floppy_drive& drive ) {

		if ( !drive.disk.has_track ( drive.cylinder, head ) ) {

			finish ( 0x40, 0x01, 0 );

			return;

		}

		id [ 0 ] = drive.cylinder;
		id [ 1 ] = head;
		id [ 2 ] = 1;
		id [ 3 ] = 2;
		finish ( 0, 0, 0 );

	}

	void fdc::move_data ( floppy_drive& drive ) {

		if ( !place ) {

			if ( task == TASK_WRITE && drive.disk.protect ) {

				finish ( 0x40, 0x02, 0 );

				return;

			}

			if ( !drive.disk.has_track ( drive.cylinder, head ) ) {

				finish ( 0x40, 0x01, 0 );

				return;

			}

			if ( id [ 0 ] != drive.cylinder ) {

				finish ( 0x40, 0x04, id [ 0 ] == 0xFF ? 0x02 : 0x10 );

				return;

			}

			if ( id [ 1 ] == head && id [ 3 ] == 2 )
				place = drive.disk.sector ( drive.cylinder, head, id [ 2 ] );

			if ( !place ) {

				finish ( 0x40, 0x04, 0 );

				return;

			}

			offset = 0;

		}

		uint8_t value = task == TASK_READ ? place [ offset ] : 0;
		dma_result moved = transfers.transfer ( 2, memory, value );

		if ( moved == DMA_BLOCKED ) {

			finish ( 0x40, 0x10, 0 );

			return;

		}

		if ( task == TASK_WRITE ) {

			place [ offset ] = value;
			drive.disk.changed = true;

		}

		terminal = terminal || moved == DMA_TERMINAL;
		offset++;

		if ( offset < floppy::sector_size && !terminal )
			return;

		place = nullptr;

		bool ended = next_record ( );

		if ( terminal )
			finish ( 0, 0, 0 );
		else if ( ended )
			finish ( 0x40, 0x80, 0 );

	}

	void fdc::format_step ( floppy_drive& drive ) {

		if ( drive.disk.protect ) {

			finish ( 0x40, 0x02, 0 );

			return;

		}

		uint8_t value = 0;
		dma_result moved = transfers.transfer ( 2, memory, value );

		if ( moved == DMA_BLOCKED ) {

			finish ( 0x40, 0x10, 0 );

			return;

		}

		id [ field++ ] = value;

		if ( field < 4 )
			return;

		field = 0;

		uint8_t* target = id [ 3 ] == 2 ? drive.disk.sector ( drive.cylinder, head, id [ 2 ] ) : nullptr;

		if ( target ) {

			for ( uint32_t i = 0; i < floppy::sector_size; i++ )
				target [ i ] = filler;

			drive.disk.changed = true;

		}

		if ( ++formatted >= format_count || moved == DMA_TERMINAL ) {

			finish ( 0, 0, 0 );

			return;

		}

		wait = byte_cycles * floppy::sector_size;

	}

	bool fdc::next_record ( ) {

		if ( id [ 2 ] != last_record ) {

			id [ 2 ]++;

			return false;

		}

		id [ 2 ] = 1;

		if ( multi_track && !head ) {

			id [ 1 ] = static_cast < uint8_t > ( id [ 1 ] ^ 1 );
			head = 1;

			return false;

		}

		id [ 0 ]++;

		if ( multi_track )
			id [ 1 ] = static_cast < uint8_t > ( id [ 1 ] ^ 1 );

		return true;

	}

	void fdc::finish ( uint8_t st0, uint8_t st1, uint8_t st2 ) {

		result [ 0 ] = static_cast < uint8_t > ( st0 | head << 2 | unit );
		result [ 1 ] = st1;
		result [ 2 ] = st2;

		for ( int32_t i = 0; i < 4; i++ )
			result [ 3 + i ] = id [ i ];

		task = TASK_NONE;
		place = nullptr;
		pending = true;
		respond ( 7, true );

	}

}