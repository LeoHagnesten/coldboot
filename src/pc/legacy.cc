#include "pc/legacy.hh"
#include <windows.h>

namespace pc {

	uint64_t host_nanoseconds ( ) {

		static LARGE_INTEGER frequency = { };
		LARGE_INTEGER now;

		if ( !frequency.QuadPart )
			QueryPerformanceFrequency ( &frequency );

		QueryPerformanceCounter ( &now );

		uint64_t ticks = static_cast < uint64_t > ( now.QuadPart );
		uint64_t rate = static_cast < uint64_t > ( frequency.QuadPart );

		return ticks / rate * 1000000000ull + ticks % rate * 1000000000ull / rate;

	}

	void dual_pic::reset ( ) {

		master.reset ( );
		slave.reset ( );
		master.elcr = 0;
		slave.elcr = 0;

	}

	void dual_pic::cascade ( ) {

		master.set_line ( 2, slave.pending ( ) );

	}

	void dual_pic::set_line ( uint32_t irq, bool level ) {

		if ( irq < 8 )
			master.set_line ( static_cast < uint8_t > ( irq ), level );
		else if ( irq < 16 ) {

			slave.set_line ( static_cast < uint8_t > ( irq - 8 ), level );
			cascade ( );

		}

	}

	bool dual_pic::asserted ( ) {

		return master.pending ( );

	}

	uint8_t dual_pic::acknowledge ( ) {

		uint8_t vector = master.acknowledge ( );

		if ( vector != ( master.vector_base | 2 ) )
			return vector;

		uint8_t inner = slave.acknowledge ( );

		master.set_line ( 2, false );
		cascade ( );

		return inner;

	}

	uint64_t dual_pic::read ( uint64_t port, uint32_t size ) {

		uint64_t value = 0;

		for ( uint32_t i = 0; i < size; i++ ) {

			uint16_t at = static_cast < uint16_t > ( port + i );
			uint8_t part = 0xFF;

			if ( at == 0x20 || at == 0x21 )
				part = master.in ( at );
			else if ( at == 0xA0 || at == 0xA1 ) {

				part = slave.in ( at );
				cascade ( );

			} else if ( at == 0x4D0 )
				part = master.elcr;
			else if ( at == 0x4D1 )
				part = slave.elcr;

			value |= static_cast < uint64_t > ( part ) << ( i * 8 );

		}

		return value;

	}

	void dual_pic::write ( uint64_t port, uint32_t size, uint64_t value ) {

		for ( uint32_t i = 0; i < size; i++ ) {

			uint16_t at = static_cast < uint16_t > ( port + i );
			uint8_t part = static_cast < uint8_t > ( value >> ( i * 8 ) );

			if ( at == 0x20 || at == 0x21 )
				master.out ( at, part );
			else if ( at == 0xA0 || at == 0xA1 ) {

				slave.out ( at, part );
				cascade ( );

			} else if ( at == 0x4D0 )
				master.elcr = static_cast < uint8_t > ( part & 0xF8 );
			else if ( at == 0x4D1 )
				slave.elcr = static_cast < uint8_t > ( part & 0xDE );

		}

	}

	void interval_timer::reset ( ) {

		chip.reset ( );
		chip.set_gate ( 2, false );
		origin = bus.now ( );
		ticks = 0;
		control = 0;
		output = chip.channels [ 0 ].output;

	}

	void interval_timer::advance ( ) {

		uint64_t elapsed = bus.now ( ) - origin;
		uint64_t target = elapsed / 1000000000ull * rate + elapsed % 1000000000ull * rate / 1000000000ull;

		if ( target > ticks + rate )
			ticks = target - rate;

		while ( ticks < target ) {

			chip.clock ( );
			ticks++;

			if ( chip.channels [ 0 ].output != output ) {

				output = chip.channels [ 0 ].output;
				bus.set_irq ( 0, output );

			}

		}

	}

	uint64_t interval_timer::read ( uint64_t port, uint32_t ) {

		advance ( );

		if ( port == 0x61 )
			return ( control & 0x0F ) | ( ( ticks / 18 ) & 1 ? 0x10 : 0 ) | ( chip.channels [ 2 ].output ? 0x20 : 0 );

		return chip.in ( static_cast < uint16_t > ( port ) );

	}

	void interval_timer::write ( uint64_t port, uint32_t, uint64_t value ) {

		advance ( );

		if ( port == 0x61 ) {

			control = static_cast < uint8_t > ( value );
			chip.set_gate ( 2, ( value & 1 ) != 0 );

			return;

		}

		chip.out ( static_cast < uint16_t > ( port ), static_cast < uint8_t > ( value ) );

	}

	static int64_t local_epoch_ticks ( ) {

		SYSTEMTIME local;
		FILETIME stamp;

		GetLocalTime ( &local );
		SystemTimeToFileTime ( &local, &stamp );

		return static_cast < int64_t > ( static_cast < uint64_t > ( stamp.dwHighDateTime ) << 32 | stamp.dwLowDateTime );

	}

	void real_time_clock::reset ( uint64_t memory_bytes ) {

		for ( uint8_t& cell : cmos )
			cell = 0;

		epoch_ticks = local_epoch_ticks ( );
		epoch_nanoseconds = bus.now ( );
		offset = 0;
		index = 0;
		line = false;
		cmos [ 0x0A ] = 0x26;
		cmos [ 0x0B ] = 0x02;
		cmos [ 0x0D ] = 0x80;
		cmos [ 0x14 ] = 0x06;
		cmos [ 0x15 ] = 0x80;
		cmos [ 0x16 ] = 0x02;

		uint64_t extended = memory_bytes > 0x100000 ? ( memory_bytes - 0x100000 ) / 1024 : 0;
		uint64_t above = memory_bytes > 0x1000000 ? ( memory_bytes - 0x1000000 ) / 0x10000 : 0;

		extended = extended > 0xFFFF ? 0xFFFF : extended;
		above = above > 0xFFFF ? 0xFFFF : above;
		cmos [ 0x17 ] = static_cast < uint8_t > ( extended );
		cmos [ 0x18 ] = static_cast < uint8_t > ( extended >> 8 );
		cmos [ 0x30 ] = cmos [ 0x17 ];
		cmos [ 0x31 ] = cmos [ 0x18 ];
		cmos [ 0x34 ] = static_cast < uint8_t > ( above );
		cmos [ 0x35 ] = static_cast < uint8_t > ( above >> 8 );
		last_second = seconds_now ( );
		last_period = 0;
		refresh ( );

	}

	int64_t real_time_clock::seconds_now ( ) const {

		int64_t ticks = epoch_ticks + static_cast < int64_t > ( ( bus.now ( ) - epoch_nanoseconds ) / 100 );

		return ticks / 10000000 + offset;

	}

	uint8_t real_time_clock::encode ( int32_t value ) const {

		return cmos [ 0x0B ] & 0x04 ? static_cast < uint8_t > ( value ) : static_cast < uint8_t > ( value / 10 * 16 + value % 10 );

	}

	int32_t real_time_clock::decode ( uint8_t value ) const {

		return cmos [ 0x0B ] & 0x04 ? value : ( value >> 4 ) * 10 + ( value & 15 );

	}

	void real_time_clock::refresh ( ) {

		if ( cmos [ 0x0B ] & 0x80 )
			return;

		int64_t ticks = seconds_now ( ) * 10000000;
		FILETIME stamp = { static_cast < DWORD > ( ticks ), static_cast < DWORD > ( static_cast < uint64_t > ( ticks ) >> 32 ) };
		SYSTEMTIME fields;

		if ( !FileTimeToSystemTime ( &stamp, &fields ) )
			return;

		int32_t hour = fields.wHour;

		if ( !( cmos [ 0x0B ] & 0x02 ) ) {

			bool afternoon = hour >= 12;

			hour = hour % 12 ? hour % 12 : 12;
			cmos [ 0x04 ] = static_cast < uint8_t > ( encode ( hour ) | ( afternoon ? 0x80 : 0 ) );

		} else
			cmos [ 0x04 ] = encode ( hour );

		cmos [ 0x00 ] = encode ( fields.wSecond );
		cmos [ 0x02 ] = encode ( fields.wMinute );
		cmos [ 0x06 ] = encode ( fields.wDayOfWeek + 1 );
		cmos [ 0x07 ] = encode ( fields.wDay );
		cmos [ 0x08 ] = encode ( fields.wMonth );
		cmos [ 0x09 ] = encode ( fields.wYear % 100 );
		cmos [ 0x32 ] = encode ( fields.wYear / 100 );

	}

	void real_time_clock::update_irq ( ) {

		uint8_t flags = cmos [ 0x0C ];
		uint8_t enables = cmos [ 0x0B ];
		bool active = ( ( flags & 0x40 ) && ( enables & 0x40 ) ) || ( ( flags & 0x20 ) && ( enables & 0x20 ) ) || ( ( flags & 0x10 ) && ( enables & 0x10 ) );

		cmos [ 0x0C ] = static_cast < uint8_t > ( active ? flags | 0x80 : flags & 0x7F );

		if ( active != line ) {

			line = active;
			bus.set_irq ( 8, line );

		}

	}

	void real_time_clock::advance ( ) {

		int64_t second = seconds_now ( );
		uint32_t rate = cmos [ 0x0A ] & 0x0F;

		if ( second != last_second ) {

			last_second = second;

			if ( !( cmos [ 0x0B ] & 0x80 ) ) {

				refresh ( );
				cmos [ 0x0C ] |= 0x10;

				bool alarm = ( ( cmos [ 0x01 ] & 0xC0 ) == 0xC0 || cmos [ 0x01 ] == cmos [ 0x00 ] ) && ( ( cmos [ 0x03 ] & 0xC0 ) == 0xC0 || cmos [ 0x03 ] == cmos [ 0x02 ] ) && ( ( cmos [ 0x05 ] & 0xC0 ) == 0xC0 || cmos [ 0x05 ] == cmos [ 0x04 ] );

				if ( alarm )
					cmos [ 0x0C ] |= 0x20;

			}

		}

		if ( rate ) {

			uint64_t period = rate <= 2 ? 1000000000ull / ( rate == 1 ? 256 : 128 ) : 1000000000ull * ( 1ull << ( rate - 1 ) ) / 32768;
			uint64_t count = bus.now ( ) / period;

			if ( count != last_period ) {

				last_period = count;
				cmos [ 0x0C ] |= 0x40;

			}

		}

		update_irq ( );

	}

	uint64_t real_time_clock::read ( uint64_t port, uint32_t ) {

		if ( port == 0x70 )
			return index;

		advance ( );

		if ( index < 10 || index == 0x32 )
			refresh ( );

		if ( index == 0x0A ) {

			uint64_t fraction = ( epoch_ticks + ( bus.now ( ) - epoch_nanoseconds ) / 100 ) % 10000000;

			return ( cmos [ 0x0A ] & 0x7F ) | ( fraction >= 9997560 ? 0x80 : 0 );

		}

		if ( index == 0x0C ) {

			uint8_t value = cmos [ 0x0C ];

			cmos [ 0x0C ] = 0;
			update_irq ( );

			return value;

		}

		return cmos [ index ];

	}

	void real_time_clock::write ( uint64_t port, uint32_t, uint64_t value ) {

		uint8_t byte = static_cast < uint8_t > ( value );

		if ( port == 0x70 ) {

			index = static_cast < uint8_t > ( byte & 0x7F );

			return;

		}

		if ( index == 0x0C || index == 0x0D )
			return;

		if ( index == 0x0A ) {

			cmos [ 0x0A ] = static_cast < uint8_t > ( byte & 0x7F );

			return;

		}

		if ( index == 0x0B ) {

			bool was_set = ( cmos [ 0x0B ] & 0x80 ) != 0;

			cmos [ 0x0B ] = byte;

			if ( was_set && !( byte & 0x80 ) ) {

				SYSTEMTIME fields = { };
				int32_t hour = decode ( static_cast < uint8_t > ( cmos [ 0x04 ] & 0x7F ) );

				if ( !( byte & 0x02 ) ) {

					hour %= 12;

					if ( cmos [ 0x04 ] & 0x80 )
						hour += 12;

				}

				fields.wYear = static_cast < WORD > ( decode ( cmos [ 0x32 ] ) * 100 + decode ( cmos [ 0x09 ] ) );
				fields.wMonth = static_cast < WORD > ( decode ( cmos [ 0x08 ] ) );
				fields.wDay = static_cast < WORD > ( decode ( cmos [ 0x07 ] ) );
				fields.wHour = static_cast < WORD > ( hour );
				fields.wMinute = static_cast < WORD > ( decode ( cmos [ 0x02 ] ) );
				fields.wSecond = static_cast < WORD > ( decode ( cmos [ 0x00 ] ) );

				FILETIME stamp;

				if ( fields.wYear < 1601 )
					fields.wYear = static_cast < WORD > ( fields.wYear + 2000 );

				if ( SystemTimeToFileTime ( &fields, &stamp ) ) {

					int64_t wanted = static_cast < int64_t > ( static_cast < uint64_t > ( stamp.dwHighDateTime ) << 32 | stamp.dwLowDateTime ) / 10000000;

					offset = 0;
					offset = wanted - seconds_now ( );
					last_second = seconds_now ( );

				}

			}

			update_irq ( );

			return;

		}

		cmos [ index ] = byte;

	}

	uint64_t system_ports::read ( uint64_t port, uint32_t ) {

		if ( port == 0x80 )
			return post_code;

		if ( port == 0x92 )
			return port92;

		return port == 0xE9 ? 0xE9 : 0xFF;

	}

	void system_ports::write ( uint64_t port, uint32_t, uint64_t value ) {

		uint8_t byte = static_cast < uint8_t > ( value );

		if ( port == 0x80 )
			post_code = byte;
		else if ( port == 0xE9 )
			bus.console ( static_cast < char > ( byte ) );
		else if ( port == 0x92 ) {

			bool reset = ( byte & 1 ) && !( port92 & 1 );

			port92 = static_cast < uint8_t > ( byte & 0x03 );

			if ( reset )
				bus.request_reset ( );

		}

	}

}