#include "pc/uhci.hh"

namespace pc {

	static constexpr uint16_t COMMAND_RUN = 0x0001;
	static constexpr uint16_t COMMAND_RESET = 0x0002;
	static constexpr uint16_t COMMAND_GLOBAL_RESET = 0x0004;
	static constexpr uint16_t STATUS_INTERRUPT = 0x0001;
	static constexpr uint16_t STATUS_ERROR = 0x0002;
	static constexpr uint16_t STATUS_HALTED = 0x0020;
	static constexpr uint16_t PORT_CONNECTED = 0x0001;
	static constexpr uint16_t PORT_CONNECT_CHANGE = 0x0002;
	static constexpr uint16_t PORT_ENABLED = 0x0004;
	static constexpr uint16_t PORT_ENABLE_CHANGE = 0x0008;
	static constexpr uint16_t PORT_RESUME = 0x0040;
	static constexpr uint16_t PORT_RESET = 0x0200;
	static constexpr uint16_t PORT_SUSPEND = 0x1000;
	static constexpr uint32_t TD_ACTIVE = 1u << 23;
	static constexpr uint32_t TD_STALLED = 1u << 22;
	static constexpr uint32_t TD_NAK = 1u << 19;
	static constexpr uint32_t TD_TIMEOUT = 1u << 18;
	static constexpr uint32_t TD_IOC = 1u << 24;
	static constexpr uint32_t TD_SHORT = 1u << 29;
	static constexpr uint32_t TD_STATUS = 0x00FF0000u;
	static constexpr uint64_t frame_time = 1000000;

	uhci::uhci ( platform_bus& owner, hv::machine& target, uint32_t line_number ) : bus ( owner ), vm ( target ), irq ( line_number ) {

		identify ( 0x8086, 0x7020, 0x0C, 0x03, 0x00, 0x01, 0x00 );
		set_bar ( 4, 32, true, default_base );
		set_interrupt ( 4, 11 );
		config [ 0x04 ] = 0x05;
		config [ 0x60 ] = 0x10;
		config [ 0xC1 ] = 0x20;
		reset ( );

	}

	void uhci::reset ( ) {

		reset_controller ( );

		for ( int32_t i = 0; i < port_count; i++ ) {

			if ( devices [ i ] )
				devices [ i ]->reset ( );

		}

	}

	void uhci::reset_controller ( ) {

		command = 0;
		status = STATUS_HALTED;
		enabled = 0;
		frame = 0;
		list = 0;
		sof = 0x40;
		global_reset = false;

		for ( int32_t i = 0; i < port_count; i++ )
			ports [ i ] = devices [ i ] ? static_cast < uint16_t > ( PORT_CONNECTED | PORT_CONNECT_CHANGE ) : 0;

		update_line ( );

	}

	void uhci::plug ( int32_t port, usb_device* device ) {

		if ( port < 0 || port >= port_count )
			return;

		devices [ port ] = device;
		ports [ port ] = device ? static_cast < uint16_t > ( PORT_CONNECTED | PORT_CONNECT_CHANGE ) : static_cast < uint16_t > ( PORT_CONNECT_CHANGE | ( ports [ port ] & PORT_ENABLED ? PORT_ENABLE_CHANGE : 0 ) );

		if ( device )
			device->reset ( );

	}

	void uhci::written ( uint32_t offset ) {

		if ( offset > 0x23 || offset < 0x20 )
			return;

		uint16_t wanted = static_cast < uint16_t > ( bar_address ( 4 ) & 0xFFE0 );

		if ( !wanted || wanted == base || wanted > 0xFFE0 )
			return;

		vm.relocate_ports ( this, base, wanted, static_cast < uint16_t > ( wanted + 31 ) );
		base = wanted;

	}

	uint16_t uhci::read_word ( uint32_t offset ) const {

		switch ( offset ) {

			case 0x00:
				return command;

			case 0x02:
				return status;

			case 0x04:
				return enabled;

			case 0x06:
				return frame;

			case 0x08:
				return static_cast < uint16_t > ( list );

			case 0x0A:
				return static_cast < uint16_t > ( list >> 16 );

			case 0x0C:
				return sof;

			case 0x10:
			case 0x12: {

				uint16_t value = static_cast < uint16_t > ( ports [ ( offset - 0x10 ) / 2 ] | 0x0080 );

				if ( ( value & PORT_CONNECTED ) && !( value & PORT_RESET ) )
					value |= 0x0010;

				return value;

			}

			default:
				return 0;

		}

	}

	void uhci::write_word ( uint32_t offset, uint16_t value, uint16_t mask ) {

		uint16_t merged = static_cast < uint16_t > ( ( read_word ( offset ) & ~mask ) | ( value & mask ) );

		switch ( offset ) {

			case 0x00:

				if ( value & mask & COMMAND_RESET ) {

					reset ( );

					return;

				}

				if ( merged & COMMAND_GLOBAL_RESET )
					global_reset = true;
				else if ( global_reset ) {

					reset ( );

					return;

				}

				if ( ( merged & COMMAND_RUN ) && !( command & COMMAND_RUN ) ) {

					status &= static_cast < uint16_t > ( ~STATUS_HALTED );
					last_frame = bus.now ( );

				} else if ( !( merged & COMMAND_RUN ) )
					status |= STATUS_HALTED;

				command = static_cast < uint16_t > ( merged & 0x00FD );
				break;

			case 0x02:
				status &= static_cast < uint16_t > ( ~( value & mask & 0x001F ) );
				update_line ( );
				break;

			case 0x04:
				enabled = static_cast < uint16_t > ( merged & 0x000F );
				update_line ( );
				break;

			case 0x06:
				frame = static_cast < uint16_t > ( merged & 0x07FF );
				break;

			case 0x08:
				list = ( list & 0xFFFF0000u ) | ( merged & 0xF000u );
				break;

			case 0x0A:
				list = ( list & 0x0000FFFFu ) | static_cast < uint32_t > ( merged ) << 16;
				break;

			case 0x0C:
				sof = static_cast < uint8_t > ( merged & 0x7F );
				break;

			case 0x10:
			case 0x12: {

				int32_t index = static_cast < int32_t > ( offset - 0x10 ) / 2;
				uint16_t& port = ports [ index ];
				uint16_t cleared = static_cast < uint16_t > ( value & mask & ( PORT_CONNECT_CHANGE | PORT_ENABLE_CHANGE ) );
				bool was_reset = ( port & PORT_RESET ) != 0;

				port &= static_cast < uint16_t > ( ~cleared );

				if ( !( mask & 0x00FF ) )
					merged = static_cast < uint16_t > ( ( merged & 0xFF00 ) | ( port & 0x00FF ) );

				if ( !( mask & 0xFF00 ) )
					merged = static_cast < uint16_t > ( ( merged & 0x00FF ) | ( port & 0xFF00 ) );

				if ( ( merged & PORT_ENABLED ) && ( port & PORT_CONNECTED ) )
					port |= PORT_ENABLED;
				else
					port &= static_cast < uint16_t > ( ~PORT_ENABLED );

				port = static_cast < uint16_t > ( ( port & ~( PORT_RESUME | PORT_SUSPEND | PORT_RESET ) ) | ( merged & ( PORT_RESUME | PORT_SUSPEND | PORT_RESET ) ) );

				if ( ( port & PORT_RESET ) && !was_reset ) {

					port &= static_cast < uint16_t > ( ~PORT_ENABLED );

					if ( devices [ index ] )
						devices [ index ]->reset ( );

				}

				break;

			}

			default:
				break;

		}

	}

	uint64_t uhci::read ( uint64_t port, uint32_t size ) {

		uint32_t offset = static_cast < uint32_t > ( port - base );
		uint64_t value = 0;

		for ( uint32_t i = 0; i < size; i++ ) {

			uint32_t at = offset + i;
			uint16_t word = read_word ( at & ~1u );

			value |= static_cast < uint64_t > ( at & 1 ? word >> 8 : word & 0xFF ) << ( i * 8 );

		}

		return value;

	}

	void uhci::write ( uint64_t port, uint32_t size, uint64_t value ) {

		uint32_t offset = static_cast < uint32_t > ( port - base );

		for ( uint32_t i = 0; i < size; ) {

			uint32_t at = offset + i;
			uint32_t word = at & ~1u;
			uint16_t data = 0;
			uint16_t mask = 0;

			for ( ; i < size && ( ( offset + i ) & ~1u ) == word; i++ ) {

				uint32_t shift = ( ( offset + i ) & 1 ) * 8;

				data = static_cast < uint16_t > ( data | ( value >> ( i * 8 ) & 0xFF ) << shift );
				mask = static_cast < uint16_t > ( mask | 0xFF << shift );

			}

			write_word ( word, data, mask );

		}

	}

	uint32_t uhci::read32 ( uint32_t address ) {

		uint8_t bytes [ 4 ] = { };

		vm.read ( address, bytes, 4 );

		return static_cast < uint32_t > ( bytes [ 0 ] ) | static_cast < uint32_t > ( bytes [ 1 ] ) << 8 | static_cast < uint32_t > ( bytes [ 2 ] ) << 16 | static_cast < uint32_t > ( bytes [ 3 ] ) << 24;

	}

	void uhci::write32 ( uint32_t address, uint32_t value ) {

		uint8_t bytes [ 4 ] = { static_cast < uint8_t > ( value ), static_cast < uint8_t > ( value >> 8 ), static_cast < uint8_t > ( value >> 16 ), static_cast < uint8_t > ( value >> 24 ) };

		vm.write ( address, bytes, 4 );

	}

	usb_device* uhci::find ( uint8_t address ) {

		for ( int32_t i = 0; i < port_count; i++ ) {

			if ( devices [ i ] && ( ports [ i ] & PORT_ENABLED ) && !( ports [ i ] & ( PORT_RESET | PORT_SUSPEND ) ) && devices [ i ]->address == address )
				return devices [ i ];

		}

		return nullptr;

	}

	uhci::td_result uhci::run_td ( uint32_t address ) {

		uint32_t control = read32 ( address + 4 );

		if ( !( control & TD_ACTIVE ) )
			return TD_SKIPPED;

		uint32_t token = read32 ( address + 8 );
		uint32_t buffer = read32 ( address + 12 );
		uint8_t pid = static_cast < uint8_t > ( token );
		uint8_t target = static_cast < uint8_t > ( token >> 8 & 0x7F );
		uint8_t endpoint = static_cast < uint8_t > ( token >> 15 & 0x0F );
		int32_t maximum = static_cast < int32_t > ( ( ( token >> 21 ) + 1 ) & 0x7FF );
		usb_device* device = find ( target );
		int32_t result = usb_timeout;

		if ( maximum > static_cast < int32_t > ( sizeof ( scratch ) ) )
			maximum = sizeof ( scratch );

		if ( device ) {

			if ( pid == pid_setup || pid == pid_out ) {

				if ( maximum )
					vm.read ( buffer, scratch, static_cast < uint32_t > ( maximum ) );

				result = device->transfer ( pid, endpoint, scratch, maximum );

			} else if ( pid == pid_in ) {

				result = device->transfer ( pid, endpoint, scratch, maximum );

				if ( result > 0 )
					vm.write ( buffer, scratch, static_cast < uint32_t > ( result < maximum ? result : maximum ) );

			} else
				result = usb_stall;

		}

		control &= ~( TD_STATUS | 0x7FFu );

		if ( result == usb_nak ) {

			write32 ( address + 4, control | TD_ACTIVE | TD_NAK | 0x7FF );

			return TD_WAITING;

		}

		if ( result < 0 ) {

			control |= ( result == usb_stall ? TD_STALLED : TD_TIMEOUT ) | 0x7FF;
			control &= ~( 3u << 27 );
			write32 ( address + 4, control );
			failed = true;

			if ( control & TD_IOC )
				completed = true;

			return TD_HALTED;

		}

		if ( result > maximum )
			result = maximum;

		control |= result ? static_cast < uint32_t > ( result - 1 ) : 0x7FFu;
		write32 ( address + 4, control );

		if ( control & TD_IOC )
			completed = true;

		if ( pid == pid_in && result < maximum && ( control & TD_SHORT ) ) {

			shorted = true;

			return TD_HALTED;

		}

		return TD_DONE;

	}

	void uhci::run_frame ( ) {

		uint32_t link = read32 ( list + ( frame & 0x3FFu ) * 4 );
		uint32_t visited [ 64 ];
		uint32_t visit_count = 0;

		frames_run++;

		for ( int32_t step = 0; step < 512 && !( link & 1 ); step++ ) {

			uint32_t at = link & ~0xFu;

			if ( !( link & 2 ) ) {

				run_td ( at );
				link = read32 ( at );

				continue;

			}

			bool seen = false;

			for ( uint32_t i = 0; i < visit_count; i++ )
				seen = seen || visited [ i ] == at;

			if ( seen || visit_count == 64 )
				break;

			visited [ visit_count++ ] = at;

			uint32_t head = read32 ( at );
			uint32_t element = read32 ( at + 4 );

			for ( int32_t depth = 0; depth < 64 && !( element & 1 ) && !( element & 2 ); depth++ ) {

				uint32_t td = element & ~0xFu;

				if ( run_td ( td ) != TD_DONE )
					break;

				element = read32 ( td );
				write32 ( at + 4, element );

				if ( !( element & 4 ) )
					break;

			}

			link = head;

		}

		frame = static_cast < uint16_t > ( ( frame + 1 ) & 0x7FF );

		if ( completed || shorted )
			status |= STATUS_INTERRUPT;

		if ( failed )
			status |= STATUS_ERROR;

		completed = false;
		shorted = false;
		failed = false;
		update_line ( );

	}

	void uhci::advance ( uint64_t now ) {

		if ( !( command & COMMAND_RUN ) ) {

			last_frame = now;

			return;

		}

		if ( now < last_frame + frame_time )
			return;

		uint64_t due = ( now - last_frame ) / frame_time;

		if ( due > 16 ) {

			due = 16;
			last_frame = now;

		} else
			last_frame += due * frame_time;

		for ( uint64_t i = 0; i < due; i++ )
			run_frame ( );

	}

	void uhci::update_line ( ) {

		bool wanted = ( status & 0x0018 ) != 0 || ( ( status & STATUS_INTERRUPT ) && ( enabled & 0x000C ) ) || ( ( status & STATUS_ERROR ) && ( enabled & 0x0001 ) ) || ( ( status & 0x0004 ) && ( enabled & 0x0002 ) );

		if ( wanted == line )
			return;

		line = wanted;
		bus.set_irq ( irq, wanted );

	}

}