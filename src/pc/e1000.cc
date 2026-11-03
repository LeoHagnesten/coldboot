#include "pc/e1000.hh"
#include <windows.h>

namespace pc {

	static constexpr uint32_t REG_CTRL = 0x0000;
	static constexpr uint32_t REG_STATUS = 0x0008;
	static constexpr uint32_t REG_EECD = 0x0010;
	static constexpr uint32_t REG_EERD = 0x0014;
	static constexpr uint32_t REG_MDIC = 0x0020;
	static constexpr uint32_t REG_ICR = 0x00C0;
	static constexpr uint32_t REG_ICS = 0x00C8;
	static constexpr uint32_t REG_IMS = 0x00D0;
	static constexpr uint32_t REG_IMC = 0x00D8;
	static constexpr uint32_t REG_RCTL = 0x0100;
	static constexpr uint32_t REG_TCTL = 0x0400;
	static constexpr uint32_t REG_LEDCTL = 0x0E00;
	static constexpr uint32_t REG_PBA = 0x1000;
	static constexpr uint32_t REG_RDBAL = 0x2800;
	static constexpr uint32_t REG_RDBAH = 0x2804;
	static constexpr uint32_t REG_RDLEN = 0x2808;
	static constexpr uint32_t REG_RDH = 0x2810;
	static constexpr uint32_t REG_RDT = 0x2818;
	static constexpr uint32_t REG_TDBAL = 0x3800;
	static constexpr uint32_t REG_TDBAH = 0x3804;
	static constexpr uint32_t REG_TDLEN = 0x3808;
	static constexpr uint32_t REG_TDH = 0x3810;
	static constexpr uint32_t REG_TDT = 0x3818;
	static constexpr uint32_t REG_GPRC = 0x4074;
	static constexpr uint32_t REG_GPTC = 0x4080;
	static constexpr uint32_t REG_GORCL = 0x4088;
	static constexpr uint32_t REG_GOTCL = 0x4090;
	static constexpr uint32_t REG_TPR = 0x40D0;
	static constexpr uint32_t REG_TPT = 0x40D4;
	static constexpr uint32_t REG_RAL = 0x5400;
	static constexpr uint32_t REG_RAH = 0x5404;
	static constexpr uint32_t CAUSE_TXDW = 0x0001;
	static constexpr uint32_t CAUSE_TXQE = 0x0002;
	static constexpr uint32_t CAUSE_LSC = 0x0004;
	static constexpr uint32_t CAUSE_RXDMT = 0x0010;
	static constexpr uint32_t CAUSE_RXO = 0x0040;
	static constexpr uint32_t CAUSE_RXT = 0x0080;
	static constexpr uint32_t CAUSE_MDAC = 0x0200;
	static constexpr uint32_t packet_room = 0x10000 + 0x4000;
	static constexpr uint32_t pending_room = 1600;

	static uint16_t le16 ( const uint8_t* at ) {

		return static_cast < uint16_t > ( at [ 0 ] | at [ 1 ] << 8 );

	}

	static uint32_t le32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24;

	}

	static uint16_t be16 ( const uint8_t* at ) {

		return static_cast < uint16_t > ( at [ 0 ] << 8 | at [ 1 ] );

	}

	static uint32_t be32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) << 24 | static_cast < uint32_t > ( at [ 1 ] ) << 16 | static_cast < uint32_t > ( at [ 2 ] ) << 8 | at [ 3 ];

	}

	static void put_be16 ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value >> 8 );
		at [ 1 ] = static_cast < uint8_t > ( value );

	}

	static void put_be32 ( uint8_t* at, uint32_t value ) {

		put_be16 ( at, value >> 16 );
		put_be16 ( at + 2, value );

	}

	e1000::e1000 ( platform_bus& owner, hv::machine& target, uint32_t line_number, const uint8_t* address ) : bus ( owner ), vm ( target ), irq ( line_number ) {

		regs = static_cast < uint32_t* > ( runtime_allocate_zero ( memory_size ) );
		packet = static_cast < uint8_t* > ( runtime_allocate_zero ( packet_room ) );
		pending = static_cast < uint8_t* > ( runtime_allocate_zero ( pending_room * 32 ) );

		for ( int32_t i = 0; i < 6; i++ )
			mac [ i ] = address [ i ];

		identify ( 0x8086, 0x100E, 0x02, 0x00, 0x00, 0x03, 0x00 );
		set_bar ( 0, memory_size, false, default_memory );
		set_bar ( 1, 64, true, default_io );
		set_interrupt ( 1, 10 );
		config [ 0x04 ] = 0x07;
		config [ 0x2C ] = 0x86;
		config [ 0x2D ] = 0x80;
		config [ 0x2E ] = 0x1E;
		config [ 0x2F ] = 0x00;

		for ( uint16_t& word : eeprom )
			word = 0xFFFF;

		for ( int32_t i = 0; i < 3; i++ )
			eeprom [ i ] = static_cast < uint16_t > ( mac [ i * 2 ] | mac [ i * 2 + 1 ] << 8 );

		eeprom [ 0x03 ] = 0x0000;
		eeprom [ 0x05 ] = 0x0100;
		eeprom [ 0x08 ] = 0xCB00;
		eeprom [ 0x09 ] = 0x0001;
		eeprom [ 0x0A ] = 0x4408;
		eeprom [ 0x0B ] = 0x001E;
		eeprom [ 0x0C ] = 0x8086;
		eeprom [ 0x0D ] = 0x100E;
		eeprom [ 0x0E ] = 0x8086;
		eeprom [ 0x0F ] = 0x3040;

		uint16_t sum = 0;

		for ( int32_t i = 0; i < 63; i++ )
			sum = static_cast < uint16_t > ( sum + eeprom [ i ] );

		eeprom [ 63 ] = static_cast < uint16_t > ( 0xBABA - sum );
		reset ( );

	}

	e1000::~e1000 ( ) {

		runtime_release ( regs );
		runtime_release ( packet );
		runtime_release ( pending );

	}

	void e1000::reset ( ) {

		for ( uint32_t i = 0; i < memory_size / 4; i++ )
			regs [ i ] = 0;

		regs [ REG_CTRL / 4 ] = 0x00000241;
		regs [ REG_STATUS / 4 ] = 0x00000083;
		regs [ REG_EECD / 4 ] = 0x00000110;
		regs [ REG_LEDCTL / 4 ] = 0x07068302;
		regs [ REG_PBA / 4 ] = 0x00100030;
		regs [ REG_RAL / 4 ] = le32 ( mac );
		regs [ REG_RAH / 4 ] = le16 ( mac + 4 ) | 0x80000000u;

		for ( uint16_t& value : phy )
			value = 0;

		phy [ 0x00 ] = 0x1140;
		phy [ 0x01 ] = 0x796D;
		phy [ 0x02 ] = 0x0141;
		phy [ 0x03 ] = 0x0C20;
		phy [ 0x04 ] = 0x0DE1;
		phy [ 0x05 ] = 0x41E1;
		phy [ 0x06 ] = 0x0001;
		phy [ 0x09 ] = 0x0E00;
		phy [ 0x0A ] = 0x3C00;
		phy [ 0x0F ] = 0x3000;
		phy [ 0x10 ] = 0x0068;
		phy [ 0x11 ] = 0xAC00;
		eeprom_command = 0;
		eeprom_bits = 0;
		eeprom_out = -1;
		eeprom_clock = false;
		eeprom_output = false;
		context = offload ( );
		packet_size = 0;
		pending_head = 0;
		pending_count = 0;
		regs [ REG_ICR / 4 ] = CAUSE_LSC;
		update_line ( );

	}

	void e1000::written ( uint32_t offset ) {

		if ( offset >= 0x10 && offset < 0x14 ) {

			uint32_t wanted = bar_address ( 0 );

			if ( wanted && wanted != memory_base ) {

				vm.relocate_memory ( this, memory_base, wanted );
				memory_base = wanted;

			}

		} else if ( offset >= 0x14 && offset < 0x18 ) {

			uint16_t wanted = static_cast < uint16_t > ( bar_address ( 1 ) & 0xFFC0 );

			if ( wanted && wanted != io_base && wanted <= 0xFFC0 ) {

				vm.relocate_ports ( this, io_base, wanted, static_cast < uint16_t > ( wanted + 63 ) );
				io_base = wanted;

			}

		}

	}

	uint64_t e1000::read ( uint64_t address, uint32_t size ) {

		if ( address >= memory_base && address < static_cast < uint64_t > ( memory_base ) + memory_size ) {

			uint32_t offset = static_cast < uint32_t > ( address - memory_base );
			uint64_t value = read_register ( offset & ~3u );

			if ( size == 8 )
				return value | static_cast < uint64_t > ( read_register ( ( offset & ~3u ) + 4 ) ) << 32;

			value >>= ( offset & 3 ) * 8;

			return size >= 4 ? value : value & ( ( 1ull << ( size * 8 ) ) - 1 );

		}

		uint32_t local = static_cast < uint32_t > ( address - io_base );

		if ( local < 4 )
			return io_address;

		if ( local < 8 )
			return read_register ( io_address & 0x1FFFC );

		return 0;

	}

	void e1000::write ( uint64_t address, uint32_t size, uint64_t value ) {

		if ( address >= memory_base && address < static_cast < uint64_t > ( memory_base ) + memory_size ) {

			uint32_t offset = static_cast < uint32_t > ( address - memory_base );
			uint32_t aligned = offset & ~3u;

			if ( size == 8 ) {

				write_register ( aligned, static_cast < uint32_t > ( value ) );
				write_register ( aligned + 4, static_cast < uint32_t > ( value >> 32 ) );

				return;

			}

			if ( size < 4 ) {

				uint32_t shift = ( offset & 3 ) * 8;
				uint32_t mask = static_cast < uint32_t > ( ( 1ull << ( size * 8 ) ) - 1 ) << shift;

				value = ( regs [ aligned / 4 ] & ~mask ) | ( static_cast < uint32_t > ( value ) << shift & mask );

			}

			write_register ( aligned, static_cast < uint32_t > ( value ) );

			return;

		}

		uint32_t local = static_cast < uint32_t > ( address - io_base );

		if ( local < 4 )
			io_address = static_cast < uint32_t > ( value );
		else if ( local < 8 )
			write_register ( io_address & 0x1FFFC, static_cast < uint32_t > ( value ) );

	}

	uint32_t e1000::read_register ( uint32_t offset ) {

		uint32_t& slot = regs [ offset / 4 ];

		if ( offset == REG_ICR ) {

			uint32_t value = slot;

			slot = 0;
			update_line ( );

			return value;

		}

		if ( offset >= 0x4000 && offset < 0x4100 ) {

			uint32_t value = slot;

			slot = 0;

			return value;

		}

		if ( offset == REG_EECD )
			return ( slot & ~0x88u ) | ( eeprom_output ? 0x08u : 0 ) | ( slot & 0x40 ? 0x80u : 0 );

		return slot;

	}

	void e1000::write_register ( uint32_t offset, uint32_t value ) {

		uint32_t& slot = regs [ offset / 4 ];

		switch ( offset ) {

			case REG_CTRL:

				if ( value & ( 1u << 26 ) ) {

					reset ( );

					return;

				}

				slot = value & ~( ( 1u << 26 ) | ( 1u << 31 ) );

				if ( value & 0x40 )
					regs [ REG_STATUS / 4 ] |= 0x02;

				return;

			case REG_STATUS:
				return;

			case REG_EECD:
				write_eeprom_control ( value );
				return;

			case REG_EERD:

				if ( value & 1 ) {

					uint32_t index = value >> 8 & 0xFF;

					slot = static_cast < uint32_t > ( index < 64 ? eeprom [ index ] : 0xFFFF ) << 16 | index << 8 | 0x10;

				}

				return;

			case REG_MDIC:
				phy_access ( value );
				return;

			case REG_ICR:
				slot &= ~value;
				update_line ( );
				return;

			case REG_ICS:
				raise ( value );
				return;

			case REG_IMS:
				regs [ REG_IMS / 4 ] |= value;
				update_line ( );
				return;

			case REG_IMC:
				regs [ REG_IMS / 4 ] &= ~value;
				update_line ( );
				return;

			case REG_RCTL:
				slot = value;
				flush_pending ( );
				return;

			case REG_TCTL:
				slot = value;
				transmit ( );
				return;

			case REG_RDLEN:
			case REG_TDLEN:
				slot = value & 0xFFF80;
				return;

			case REG_RDH:
			case REG_TDH:
				slot = value & 0xFFFF;
				return;

			case REG_RDT:
				slot = value & 0xFFFF;
				flush_pending ( );
				return;

			case REG_TDT:
				slot = value & 0xFFFF;
				transmit ( );
				return;

			default:
				slot = value;
				return;

		}

	}

	void e1000::write_eeprom_control ( uint32_t value ) {

		bool select = ( value & 0x02 ) != 0;
		bool clock = ( value & 0x01 ) != 0;
		uint32_t bit = value >> 2 & 1;

		regs [ REG_EECD / 4 ] = ( value & 0x77 ) | 0x100;

		if ( !select ) {

			eeprom_command = 0;
			eeprom_bits = 0;
			eeprom_out = -1;
			eeprom_output = false;
			eeprom_clock = clock;

			return;

		}

		if ( clock && !eeprom_clock ) {

			if ( eeprom_out >= 0 ) {

				uint16_t word = eeprom [ eeprom_address & 63 ];

				eeprom_output = ( word >> ( 15 - eeprom_out ) & 1 ) != 0;

				if ( ++eeprom_out == 16 ) {

					eeprom_out = 0;
					eeprom_address++;

				}

			} else if ( eeprom_bits || bit ) {

				eeprom_command = eeprom_command << 1 | bit;

				if ( ++eeprom_bits == 9 ) {

					if ( ( eeprom_command >> 6 & 7 ) == 6 ) {

						eeprom_address = eeprom_command & 63;
						eeprom_out = 0;
						eeprom_output = false;

					}

					eeprom_command = 0;
					eeprom_bits = 0;

				}

			}

		}

		eeprom_clock = clock;

	}

	void e1000::phy_access ( uint32_t value ) {

		uint32_t index = value >> 16 & 31;
		uint32_t address = value >> 21 & 31;
		uint32_t operation = value >> 26 & 3;
		uint32_t result = value & 0x3FFF0000u;

		if ( address != 1 )
			result |= 1u << 30;
		else if ( operation == 2 )
			result |= phy [ index ];
		else if ( operation == 1 ) {

			uint16_t data = static_cast < uint16_t > ( value );

			if ( index == 0 )
				data &= static_cast < uint16_t > ( ~( 0x8000 | 0x0200 ) );

			if ( index != 0x01 && index != 0x02 && index != 0x03 && index != 0x05 && index != 0x0A && index != 0x11 )
				phy [ index ] = data;

			result |= value & 0xFFFF;

		}

		regs [ REG_MDIC / 4 ] = result | 1u << 28;

		if ( value & ( 1u << 29 ) )
			raise ( CAUSE_MDAC );

	}

	void e1000::raise ( uint32_t cause ) {

		regs [ REG_ICR / 4 ] |= cause;
		update_line ( );

	}

	void e1000::update_line ( ) {

		bool wanted = ( regs [ REG_ICR / 4 ] & regs [ REG_IMS / 4 ] ) != 0;

		if ( wanted == line )
			return;

		line = wanted;
		bus.set_irq ( irq, wanted );

	}

	void e1000::transmit ( ) {

		if ( !( regs [ REG_TCTL / 4 ] & 0x02 ) )
			return;

		uint64_t base = static_cast < uint64_t > ( regs [ REG_TDBAH / 4 ] ) << 32 | ( regs [ REG_TDBAL / 4 ] & ~0xFu );
		uint32_t count = regs [ REG_TDLEN / 4 ] / 16;
		uint32_t head = regs [ REG_TDH / 4 ];
		uint32_t tail = regs [ REG_TDT / 4 ];
		bool any = false;

		if ( !count || head >= count || tail >= count )
			return;

		for ( uint32_t guard = 0; head != tail && guard < count; guard++ ) {

			uint8_t item [ 16 ];

			vm.read ( base + head * 16ull, item, 16 );

			uint64_t address = static_cast < uint64_t > ( le32 ( item + 4 ) ) << 32 | le32 ( item );
			uint32_t lower = le32 ( item + 8 );
			uint8_t command = static_cast < uint8_t > ( lower >> 24 );
			bool report = ( command & 0x08 ) != 0;

			if ( command & 0x20 ) {

				uint32_t type = lower >> 20 & 0x0F;

				if ( type == 0 ) {

					context.ip_start = item [ 0 ];
					context.ip_offset = item [ 1 ];
					context.ip_end = le16 ( item + 2 );
					context.transport_start = item [ 4 ];
					context.transport_offset = item [ 5 ];
					context.transport_end = le16 ( item + 6 );
					context.payload = lower & 0xFFFFF;
					context.command = command;
					context.header = item [ 13 ];
					context.segment = le16 ( item + 14 );

				} else if ( type == 1 ) {

					uint32_t length = lower & 0xFFFFF;

					if ( !packet_size ) {

						packet_options = item [ 13 ];
						packet_segmented = ( command & 0x04 ) != 0;

					}

					if ( packet_size + length <= packet_room ) {

						vm.read ( address, packet + packet_size, length );
						packet_size += length;

					}

					if ( command & 0x01 )
						finish_packet ( packet_segmented, packet_options );

				}

			} else {

				uint32_t length = lower & 0xFFFF;

				if ( packet_size + length <= packet_room ) {

					vm.read ( address, packet + packet_size, length );
					packet_size += length;

				}

				if ( command & 0x01 ) {

					if ( command & 0x04 )
						insert_checksum ( packet, packet_size, item [ 13 ], packet_size, lower >> 16 & 0xFF );

					emit ( packet, packet_size );
					packet_size = 0;

				}

			}

			if ( report ) {

				uint8_t done = static_cast < uint8_t > ( item [ 12 ] | 1 );

				vm.write ( base + head * 16ull + 12, &done, 1 );

			}

			head = ( head + 1 ) % count;
			any = true;

		}

		regs [ REG_TDH / 4 ] = head;

		if ( any )
			raise ( CAUSE_TXDW | ( head == tail ? CAUSE_TXQE : 0 ) );

	}

	void e1000::insert_checksum ( uint8_t* frame, uint32_t size, uint32_t start, uint32_t end, uint32_t at ) {

		if ( end > size )
			end = size;

		if ( start >= end || at + 2 > size )
			return;

		put_be16 ( frame + at, net::fold ( net::sum_words ( frame + start, end - start, 0 ) ) );

	}

	void e1000::finish_packet ( bool segmented, uint8_t options ) {

		uint32_t size = packet_size;

		packet_size = 0;

		if ( !segmented ) {

			if ( options & 0x02 )
				insert_checksum ( packet, size, context.transport_start, context.transport_end ? context.transport_end + 1u : size, context.transport_offset );

			if ( options & 0x01 )
				insert_checksum ( packet, size, context.ip_start, context.ip_end ? context.ip_end + 1u : size, context.ip_offset );

			emit ( packet, size );

			return;

		}

		uint32_t header = context.header;
		uint32_t segment = context.segment;
		uint32_t ip = context.ip_start;
		uint32_t transport = context.transport_start;
		bool tcp = ( context.command & 0x01 ) != 0;
		bool version4 = ( context.command & 0x02 ) != 0;

		if ( header > size || header <= transport || !segment || header + segment > 0x4000 ) {

			emit ( packet, size );

			return;

		}

		uint8_t* out = packet + 0x10000;
		uint32_t total = size - header;
		uint16_t identity = version4 ? be16 ( packet + ip + 4 ) : 0;
		uint32_t sequence = tcp ? be32 ( packet + transport + 4 ) : 0;

		for ( uint32_t offset = 0, index = 0; offset < total || ( !total && !index ); index++ ) {

			uint32_t chunk = total - offset < segment ? total - offset : segment;
			bool last = offset + chunk >= total;
			uint32_t length = header + chunk;

			for ( uint32_t i = 0; i < header; i++ )
				out [ i ] = packet [ i ];

			for ( uint32_t i = 0; i < chunk; i++ )
				out [ header + i ] = packet [ header + offset + i ];

			uint32_t transport_length = length - transport;
			uint32_t pseudo = 0;

			if ( version4 ) {

				uint32_t ip_header = ( out [ ip ] & 0x0F ) * 4u;

				put_be16 ( out + ip + 2, length - ip );
				put_be16 ( out + ip + 4, identity + index );
				put_be16 ( out + ip + 10, 0 );
				put_be16 ( out + ip + 10, net::fold ( net::sum_words ( out + ip, ip_header, 0 ) ) );
				pseudo = net::sum_words ( out + ip + 12, 8, 0 );

			} else {

				put_be16 ( out + ip + 4, length - ip - 40 );
				pseudo = net::sum_words ( out + ip + 8, 32, 0 );

			}

			pseudo += ( tcp ? 6u : 17u ) + transport_length;

			if ( tcp ) {

				put_be32 ( out + transport + 4, sequence + offset );

				if ( !last )
					out [ transport + 13 ] &= static_cast < uint8_t > ( ~0x09 );

				put_be16 ( out + transport + 16, 0 );
				put_be16 ( out + transport + 16, net::fold ( net::sum_words ( out + transport, transport_length, pseudo ) ) );

			} else {

				put_be16 ( out + transport + 4, transport_length );
				put_be16 ( out + transport + 6, 0 );
				put_be16 ( out + transport + 6, net::fold ( net::sum_words ( out + transport, transport_length, pseudo ) ) );

			}

			emit ( out, length );
			offset += chunk;

			if ( !total )
				break;

		}

	}

	void e1000::emit ( uint8_t* frame, uint32_t size ) {

		if ( size < 14 )
			return;

		InterlockedIncrement ( &frames_sent );
		regs [ REG_GPTC / 4 ]++;
		regs [ REG_TPT / 4 ]++;
		regs [ REG_GOTCL / 4 ] += size;

		if ( uplink )
			uplink->send ( frame, size );

	}

	bool e1000::deliver ( const uint8_t* frame, uint32_t size ) {

		uint32_t control = regs [ REG_RCTL / 4 ];

		if ( !( control & 0x02 ) || size > 1600 )
			return true;

		uint8_t data [ 1700 ];
		uint32_t total = size < 60 ? 60 : size;

		for ( uint32_t i = 0; i < total; i++ )
			data [ i ] = i < size ? frame [ i ] : 0;

		if ( !( control & ( 1u << 26 ) ) ) {

			uint32_t crc = net::crc32 ( data, total );

			for ( int32_t i = 0; i < 4; i++ )
				data [ total++ ] = static_cast < uint8_t > ( crc >> ( i * 8 ) );

		}

		static const uint32_t sizes [ 4 ] = { 2048, 1024, 512, 256 };
		uint32_t buffer = sizes [ control >> 16 & 3 ];

		if ( ( control & ( 1u << 25 ) ) && ( control >> 16 & 3 ) )
			buffer *= 16;

		uint64_t base = static_cast < uint64_t > ( regs [ REG_RDBAH / 4 ] ) << 32 | ( regs [ REG_RDBAL / 4 ] & ~0xFu );
		uint32_t count = regs [ REG_RDLEN / 4 ] / 16;
		uint32_t head = regs [ REG_RDH / 4 ];
		uint32_t tail = regs [ REG_RDT / 4 ];

		if ( !count || head >= count || tail >= count )
			return false;

		uint32_t available = ( tail + count - head ) % count;
		uint32_t needed = ( total + buffer - 1 ) / buffer;

		if ( available < needed )
			return false;

		for ( uint32_t done = 0; done < total; ) {

			uint32_t chunk = total - done < buffer ? total - done : buffer;
			uint8_t item [ 16 ];
			uint64_t slot = base + head * 16ull;

			vm.read ( slot, item, 16 );

			uint64_t address = static_cast < uint64_t > ( le32 ( item + 4 ) ) << 32 | le32 ( item );
			bool last = done + chunk >= total;
			uint8_t status [ 8 ] = { static_cast < uint8_t > ( chunk ), static_cast < uint8_t > ( chunk >> 8 ), 0, 0, static_cast < uint8_t > ( 0x05 | ( last ? 0x02 : 0 ) ), 0, 0, 0 };

			vm.write ( address, data + done, chunk );
			vm.write ( slot + 8, status, 8 );
			head = ( head + 1 ) % count;
			done += chunk;

		}

		regs [ REG_RDH / 4 ] = head;
		regs [ REG_GPRC / 4 ]++;
		regs [ REG_TPR / 4 ]++;
		regs [ REG_GORCL / 4 ] += total;
		InterlockedIncrement ( &frames_received );

		uint32_t left = available - needed;
		uint32_t threshold = count >> ( 1 + ( control >> 8 & 3 ) );

		raise ( CAUSE_RXT | ( left <= threshold ? CAUSE_RXDMT : 0 ) );

		return true;

	}

	void e1000::flush_pending ( ) {

		while ( pending_count && deliver ( pending + pending_head * pending_room, pending_sizes [ pending_head ] ) ) {

			pending_head = ( pending_head + 1 ) % 32;
			pending_count--;

		}

	}

	void e1000::receive ( const uint8_t* frame, uint32_t size ) {

		if ( size > pending_room )
			return;

		vm.lock_devices ( );
		flush_pending ( );

		if ( pending_count || !deliver ( frame, size ) ) {

			if ( pending_count < 32 ) {

				uint32_t slot = ( pending_head + pending_count ) % 32;

				for ( uint32_t i = 0; i < size; i++ )
					pending [ slot * pending_room + i ] = frame [ i ];

				pending_sizes [ slot ] = size;
				pending_count++;

			} else
				raise ( CAUSE_RXO );

		}

		vm.unlock_devices ( );

	}

}