#include "cpu/cpu.hh"

namespace machine {

	static bool even_parity ( uint8_t value ) {

		return !__builtin_parity ( value );

	}

	struct runner {

		cpu& core;
		registers& r;
		bus& memory;
		int32_t segment_override = -1;
		uint8_t repeat = 0;
		uint8_t opcode = 0;
		uint8_t mod = 0;
		uint8_t reg = 0;
		uint8_t rm = 0;
		uint16_t ea = 0;
		uint8_t ea_segment = SEG_DS;
		uint32_t ea_clocks = 0;
		uint32_t prefixes = 0;
		uint32_t iterations = 0;
		uint32_t extra = 0;
		uint32_t fetched = 0;
		uint32_t transfers = 0;
		bool decoded = false;
		bool taken = false;
		bool faulted = false;

		runner ( cpu& owner ) : core ( owner ), r ( owner.state ), memory ( owner.system ) {

		}

		uint8_t fetch8 ( ) {

			uint8_t value = memory.read8 ( cpu::linear ( r.segments [ SEG_CS ], r.ip ) );

			r.ip = static_cast < uint16_t > ( r.ip + 1 );
			fetched++;

			return value;

		}

		uint16_t fetch16 ( ) {

			uint16_t low = fetch8 ( );

			return static_cast < uint16_t > ( low | fetch8 ( ) << 8 );

		}

		uint8_t read8 ( uint8_t segment_index, uint16_t offset ) {

			transfers++;

			return memory.read8 ( cpu::linear ( r.segments [ segment_index ], offset ) );

		}

		uint16_t read16 ( uint8_t segment_index, uint16_t offset ) {

			uint16_t low = read8 ( segment_index, offset );

			return static_cast < uint16_t > ( low | read8 ( segment_index, static_cast < uint16_t > ( offset + 1 ) ) << 8 );

		}

		void write8 ( uint8_t segment_index, uint16_t offset, uint8_t value ) {

			transfers++;
			memory.write8 ( cpu::linear ( r.segments [ segment_index ], offset ), value );

		}

		void write16 ( uint8_t segment_index, uint16_t offset, uint16_t value ) {

			write8 ( segment_index, offset, static_cast < uint8_t > ( value ) );
			write8 ( segment_index, static_cast < uint16_t > ( offset + 1 ), static_cast < uint8_t > ( value >> 8 ) );

		}

		uint8_t& reg8 ( uint8_t index ) {

			return reinterpret_cast < uint8_t* > ( &r.general [ index & 3 ] ) [ index >> 2 ];

		}

		uint16_t& reg16 ( uint8_t index ) {

			return r.general [ index & 7 ];

		}

		void push ( uint16_t value ) {

			r.general [ REG_SP ] = static_cast < uint16_t > ( r.general [ REG_SP ] - 2 );
			write16 ( SEG_SS, r.general [ REG_SP ], value );

		}

		uint16_t pop ( ) {

			uint16_t value = read16 ( SEG_SS, r.general [ REG_SP ] );

			r.general [ REG_SP ] = static_cast < uint16_t > ( r.general [ REG_SP ] + 2 );

			return value;

		}

		void decode_modrm ( ) {

			uint8_t value = fetch8 ( );

			mod = value >> 6;
			reg = ( value >> 3 ) & 7;
			rm = value & 7;
			decoded = true;

			if ( mod == 3 )
				return;

			static const uint8_t base_clocks [ 8 ] = { 7, 8, 8, 7, 5, 5, 5, 5 };

			ea_clocks = mod == 0 && rm == 6 ? 6 : base_clocks [ rm ] + ( mod ? 4 : 0 );

			uint16_t base = 0;
			uint8_t segment_index = SEG_DS;

			switch ( rm ) {

				case 0:
					base = static_cast < uint16_t > ( r.general [ REG_BX ] + r.general [ REG_SI ] );
					break;

				case 1:
					base = static_cast < uint16_t > ( r.general [ REG_BX ] + r.general [ REG_DI ] );
					break;

				case 2:
					base = static_cast < uint16_t > ( r.general [ REG_BP ] + r.general [ REG_SI ] );
					segment_index = SEG_SS;
					break;

				case 3:
					base = static_cast < uint16_t > ( r.general [ REG_BP ] + r.general [ REG_DI ] );
					segment_index = SEG_SS;
					break;

				case 4:
					base = r.general [ REG_SI ];
					break;

				case 5:
					base = r.general [ REG_DI ];
					break;

				case 6:

					if ( mod == 0 )
						base = fetch16 ( );
					else {

						base = r.general [ REG_BP ];
						segment_index = SEG_SS;

					}

					break;

				default:
					base = r.general [ REG_BX ];
					break;

			}

			uint16_t displacement = 0;

			if ( mod == 1 )
				displacement = static_cast < uint16_t > ( static_cast < int16_t > ( static_cast < int8_t > ( fetch8 ( ) ) ) );
			else if ( mod == 2 )
				displacement = fetch16 ( );

			ea = static_cast < uint16_t > ( base + displacement );
			ea_segment = segment_override >= 0 ? static_cast < uint8_t > ( segment_override ) : segment_index;

		}

		uint8_t get_rm8 ( ) {

			return mod == 3 ? reg8 ( rm ) : read8 ( ea_segment, ea );

		}

		uint16_t get_rm16 ( ) {

			return mod == 3 ? reg16 ( rm ) : read16 ( ea_segment, ea );

		}

		void set_rm8 ( uint8_t value ) {

			if ( mod == 3 )
				reg8 ( rm ) = value;
			else
				write8 ( ea_segment, ea, value );

		}

		void set_rm16 ( uint16_t value ) {

			if ( mod == 3 )
				reg16 ( rm ) = value;
			else
				write16 ( ea_segment, ea, value );

		}

		uint8_t data_segment ( ) const {

			return segment_override >= 0 ? static_cast < uint8_t > ( segment_override ) : SEG_DS;

		}

		void set_flag ( uint16_t bit, bool on ) {

			if ( on )
				r.flags = static_cast < uint16_t > ( r.flags | bit );
			else
				r.flags = static_cast < uint16_t > ( r.flags & ~bit );

		}

		bool flag ( uint16_t bit ) const {

			return ( r.flags & bit ) != 0;

		}

		void result_flags ( uint32_t value, bool word ) {

			uint32_t mask = word ? 0xFFFF : 0xFF;
			uint32_t sign = word ? 0x8000 : 0x80;

			set_flag ( FLAG_ZERO, ( value & mask ) == 0 );
			set_flag ( FLAG_SIGN, ( value & sign ) != 0 );
			set_flag ( FLAG_PARITY, even_parity ( static_cast < uint8_t > ( value ) ) );

		}

		uint16_t add ( uint16_t a, uint16_t b, uint32_t carry, bool word ) {

			uint32_t mask = word ? 0xFFFF : 0xFF;
			uint32_t sign = word ? 0x8000 : 0x80;
			uint32_t result = static_cast < uint32_t > ( a ) + b + carry;

			set_flag ( FLAG_CARRY, result > mask );
			set_flag ( FLAG_AUXILIARY, ( ( a ^ b ^ result ) & 0x10 ) != 0 );
			set_flag ( FLAG_OVERFLOW, ( ( a ^ result ) & ( b ^ result ) & sign ) != 0 );
			result_flags ( result, word );

			return static_cast < uint16_t > ( result & mask );

		}

		uint16_t subtract ( uint16_t a, uint16_t b, uint32_t borrow, bool word ) {

			uint32_t mask = word ? 0xFFFF : 0xFF;
			uint32_t sign = word ? 0x8000 : 0x80;
			uint32_t result = static_cast < uint32_t > ( a ) - b - borrow;

			set_flag ( FLAG_CARRY, static_cast < uint32_t > ( a ) < static_cast < uint32_t > ( b ) + borrow );
			set_flag ( FLAG_AUXILIARY, ( ( a ^ b ^ result ) & 0x10 ) != 0 );
			set_flag ( FLAG_OVERFLOW, ( ( a ^ b ) & ( a ^ result ) & sign ) != 0 );
			result_flags ( result, word );

			return static_cast < uint16_t > ( result & mask );

		}

		uint16_t logic ( uint16_t result, bool word ) {

			set_flag ( FLAG_CARRY, false );
			set_flag ( FLAG_OVERFLOW, false );
			set_flag ( FLAG_AUXILIARY, false );
			result_flags ( result, word );

			return result;

		}

		uint16_t alu ( uint8_t operation, uint16_t a, uint16_t b, bool word ) {

			switch ( operation & 7 ) {

				case 0:
					return add ( a, b, 0, word );

				case 1:
					return logic ( static_cast < uint16_t > ( a | b ), word );

				case 2:
					return add ( a, b, flag ( FLAG_CARRY ) ? 1 : 0, word );

				case 3:
					return subtract ( a, b, flag ( FLAG_CARRY ) ? 1 : 0, word );

				case 4:
					return logic ( static_cast < uint16_t > ( a & b ), word );

				case 5:
				case 7:
					return subtract ( a, b, 0, word );

				default:
					return logic ( static_cast < uint16_t > ( a ^ b ), word );

			}

		}

		uint16_t increment ( uint16_t value, int32_t delta, bool word ) {

			bool carry = flag ( FLAG_CARRY );
			uint16_t result = delta > 0 ? add ( value, 1, 0, word ) : subtract ( value, 1, 0, word );

			set_flag ( FLAG_CARRY, carry );

			return result;

		}

		uint16_t shift ( uint8_t operation, uint16_t value, uint8_t count, bool word ) {

			if ( !count )
				return value;

			uint32_t bits = word ? 16 : 8;
			uint32_t mask = word ? 0xFFFF : 0xFF;
			uint32_t sign = word ? 0x8000 : 0x80;
			uint32_t result = value;
			uint32_t before = value;
			bool carry = flag ( FLAG_CARRY );

			for ( uint32_t i = 0; i < count; i++ ) {

				before = result;

				switch ( operation & 7 ) {

					case 0:
						carry = ( result & sign ) != 0;
						result = ( ( result << 1 ) | ( carry ? 1 : 0 ) ) & mask;
						break;

					case 1:
						carry = ( result & 1 ) != 0;
						result = ( result >> 1 ) | ( carry ? sign : 0 );
						break;

					case 2: {

						bool out = ( result & sign ) != 0;

						result = ( ( result << 1 ) | ( carry ? 1 : 0 ) ) & mask;
						carry = out;

						break;

					}

					case 3: {

						bool out = ( result & 1 ) != 0;

						result = ( result >> 1 ) | ( carry ? sign : 0 );
						carry = out;

						break;

					}

					case 4:
						carry = ( result & sign ) != 0;
						result = ( result << 1 ) & mask;
						break;

					case 5:
						carry = ( result & 1 ) != 0;
						result >>= 1;
						break;

					case 6:
						carry = false;
						result = mask;
						break;

					default:
						carry = ( result & 1 ) != 0;
						result = ( result >> 1 ) | ( result & sign );
						break;

				}

			}

			set_flag ( FLAG_CARRY, carry );

			bool top = ( result & sign ) != 0;
			bool next = ( result & ( sign >> 1 ) ) != 0;

			switch ( operation & 7 ) {

				case 0:
				case 2:
				case 4:
					set_flag ( FLAG_OVERFLOW, top != carry );
					break;

				case 1:
				case 3:
					set_flag ( FLAG_OVERFLOW, top != next );
					break;

				case 5:
					set_flag ( FLAG_OVERFLOW, ( before & sign ) != 0 );
					break;

				default:
					set_flag ( FLAG_OVERFLOW, false );
					break;

			}

			if ( ( operation & 7 ) >= 4 ) {

				result_flags ( result, word );
				set_flag ( FLAG_AUXILIARY, ( operation & 7 ) == 4 ? ( result & 0x10 ) != 0 : false );

			}

			( void ) bits;

			return static_cast < uint16_t > ( result );

		}

		bool condition ( uint8_t code ) {

			bool result = false;

			switch ( ( code >> 1 ) & 7 ) {

				case 0:
					result = flag ( FLAG_OVERFLOW );
					break;

				case 1:
					result = flag ( FLAG_CARRY );
					break;

				case 2:
					result = flag ( FLAG_ZERO );
					break;

				case 3:
					result = flag ( FLAG_CARRY ) || flag ( FLAG_ZERO );
					break;

				case 4:
					result = flag ( FLAG_SIGN );
					break;

				case 5:
					result = flag ( FLAG_PARITY );
					break;

				case 6:
					result = flag ( FLAG_SIGN ) != flag ( FLAG_OVERFLOW );
					break;

				default:
					result = flag ( FLAG_ZERO ) || flag ( FLAG_SIGN ) != flag ( FLAG_OVERFLOW );
					break;

			}

			return ( code & 1 ) ? !result : result;

		}

		void jump_short ( bool taken ) {

			int8_t displacement = static_cast < int8_t > ( fetch8 ( ) );

			this->taken = taken;

			if ( taken )
				r.ip = static_cast < uint16_t > ( r.ip + displacement );

		}

		void raise ( uint8_t vector ) {

			core.interrupt ( vector );

		}

		void load_flags ( uint16_t value ) {

			if ( !flag ( FLAG_INTERRUPT ) && ( value & FLAG_INTERRUPT ) )
				core.inhibit = true;

			r.flags = static_cast < uint16_t > ( ( value & 0x0FD5 ) | FLAG_FIXED );

		}

		void fault ( ) {

			faulted = true;
			core.interrupt ( 0 );

		}

		struct division {

			uint16_t a;
			uint16_t b;
			uint16_t c;
			bool carry;
			bool negate;
			uint32_t steps;

		};

		void negate_operands ( division& d, bool word, bool skip ) {

			uint16_t sign = word ? 0x8000 : 0x80;

			if ( !skip ) {

				bool borrow = d.c != 0;

				d.c = static_cast < uint16_t > ( -d.c );
				d.a = borrow ? static_cast < uint16_t > ( ~d.a ) : static_cast < uint16_t > ( -d.a );
				d.negate = !d.negate;

			}

			d.carry = ( d.b & sign ) != 0;
			set_flag ( FLAG_CARRY, d.carry );

			if ( d.carry ) {

				d.b = static_cast < uint16_t > ( -d.b );
				d.negate = !d.negate;

			}

		}

		bool long_divide ( division& d, bool word ) {

			uint16_t mask = word ? 0xFFFF : 0xFF;
			uint16_t sign = word ? 0x8000 : 0x80;
			uint16_t a = d.a & mask;
			uint16_t b = d.b & mask;
			uint16_t c = d.c & mask;

			subtract ( a, b, 0, word );

			bool carry = flag ( FLAG_CARRY );

			if ( !carry )
				return false;

			for ( int32_t count = word ? 16 : 8; count > 0; count-- ) {

				bool out = ( c & sign ) != 0;

				c = static_cast < uint16_t > ( ( ( c << 1 ) | ( carry ? 1 : 0 ) ) & mask );
				carry = out;
				out = ( a & sign ) != 0;
				a = static_cast < uint16_t > ( ( ( a << 1 ) | ( carry ? 1 : 0 ) ) & mask );
				carry = out;

				if ( carry ) {

					carry = false;
					a = static_cast < uint16_t > ( ( a - b ) & mask );
					d.steps++;

					continue;

				}

				subtract ( a, b, 0, word );
				carry = flag ( FLAG_CARRY );

				if ( !carry ) {

					a = static_cast < uint16_t > ( ( a - b ) & mask );
					d.steps++;

				}

			}

			c = static_cast < uint16_t > ( ( ( c << 1 ) | ( carry ? 1 : 0 ) ) & mask );
			d.carry = ( c & sign ) != 0;
			set_flag ( FLAG_CARRY, d.carry );
			d.a = a;
			d.c = c;

			return true;

		}

		void divide ( bool word, bool is_signed ) {

			uint16_t mask = word ? 0xFFFF : 0xFF;
			uint16_t sign = word ? 0x8000 : 0x80;
			uint16_t high = word ? r.general [ REG_DX ] : reg8 ( 4 );
			division d = { };

			d.a = high;
			d.c = word ? r.general [ REG_AX ] : reg8 ( 0 );
			d.b = word ? get_rm16 ( ) : get_rm8 ( );
			d.negate = repeat != 0;

			if ( is_signed )
				negate_operands ( d, word, ( d.a & sign ) == 0 );

			uint32_t fastest = is_signed ? ( word ? 165 : 101 ) : ( word ? 144 : 80 );
			uint32_t slowest = is_signed ? ( word ? 184 : 112 ) : ( word ? 162 : 90 );

			extra = 16;

			if ( !long_divide ( d, word ) ) {

				fault ( );

				return;

			}

			uint16_t quotient = static_cast < uint16_t > ( ~d.c );

			extra = base::min ( fastest + d.steps, slowest );

			if ( is_signed ) {

				if ( !d.carry ) {

					fault ( );

					return;

				}

				if ( high & sign )
					d.a = static_cast < uint16_t > ( -d.a );

				quotient = d.negate ? static_cast < uint16_t > ( d.c + 1 ) : static_cast < uint16_t > ( ~d.c );
				set_flag ( FLAG_CARRY, false );
				set_flag ( FLAG_OVERFLOW, false );

			}

			if ( word ) {

				r.general [ REG_AX ] = quotient;
				r.general [ REG_DX ] = d.a;

				return;

			}

			reg8 ( 0 ) = static_cast < uint8_t > ( quotient & mask );
			reg8 ( 4 ) = static_cast < uint8_t > ( d.a & mask );

		}

		void multiply ( bool word, bool is_signed ) {

			uint16_t mask = word ? 0xFFFF : 0xFF;
			uint16_t sign = word ? 0x8000 : 0x80;
			division d = { };

			d.c = word ? r.general [ REG_AX ] : reg8 ( 0 );
			d.b = word ? get_rm16 ( ) : get_rm8 ( );
			d.negate = repeat != 0;
			d.carry = ( d.c & sign ) != 0;

			if ( is_signed ) {

				if ( d.carry ) {

					d.c = static_cast < uint16_t > ( -d.c );
					d.negate = !d.negate;

				}

				negate_operands ( d, word, true );

			}

			uint16_t b = d.b & mask;
			uint16_t c = d.c & mask;
			uint16_t a = 0;
			bool carry = ( c & 1 ) != 0;

			c = static_cast < uint16_t > ( ( c >> 1 ) | ( d.carry ? sign : 0 ) );

			for ( int32_t count = word ? 16 : 8; count > 0; count-- ) {

				if ( carry ) {

					uint32_t sum = static_cast < uint32_t > ( a ) + b;

					carry = sum > mask;
					a = static_cast < uint16_t > ( sum & mask );
					d.steps++;

				}

				bool out = ( a & 1 ) != 0;

				a = static_cast < uint16_t > ( ( a >> 1 ) | ( carry ? sign : 0 ) );
				carry = out;
				out = ( c & 1 ) != 0;
				c = static_cast < uint16_t > ( ( c >> 1 ) | ( carry ? sign : 0 ) );
				carry = out;

			}

			d.a = a;
			d.c = c;

			uint32_t fastest = is_signed ? ( word ? 128 : 80 ) : ( word ? 118 : 70 );
			uint32_t slowest = is_signed ? ( word ? 154 : 98 ) : ( word ? 133 : 77 );

			extra = base::min ( fastest + d.steps + ( d.negate ? 6 : 0 ), slowest );

			if ( d.negate )
				negate_operands ( d, word, false );

			if ( is_signed ) {

				uint16_t low_sign = ( d.c & sign ) ? 1 : 0;
				uint16_t sum = static_cast < uint16_t > ( d.a + low_sign );

				set_flag ( FLAG_AUXILIARY, ( ( d.a ^ sum ) & 0x10 ) != 0 );
				result_flags ( sum, true );
				set_flag ( FLAG_CARRY, sum != 0 );
				set_flag ( FLAG_OVERFLOW, sum != 0 );

			} else {

				set_flag ( FLAG_CARRY, d.a != 0 );
				set_flag ( FLAG_OVERFLOW, d.a != 0 );

			}

			if ( word ) {

				r.general [ REG_AX ] = d.c;
				r.general [ REG_DX ] = d.a;

			} else
				r.general [ REG_AX ] = static_cast < uint16_t > ( d.a << 8 | ( d.c & 0xFF ) );

			if ( !is_signed ) {

				result_flags ( word ? r.general [ REG_DX ] : reg8 ( 4 ), word );
				set_flag ( FLAG_AUXILIARY, false );

			}

		}

		void string_operation ( ) {

			bool word = opcode & 1;
			int16_t step = static_cast < int16_t > ( flag ( FLAG_DIRECTION ) ? ( word ? -2 : -1 ) : ( word ? 2 : 1 ) );
			uint8_t source = data_segment ( );
			uint8_t kind = static_cast < uint8_t > ( opcode & 0xFE );

			while ( true ) {

				if ( repeat ) {

					if ( !r.general [ REG_CX ] )
						return;

				}

				switch ( kind ) {

					case 0xA4:

						if ( word )
							write16 ( SEG_ES, r.general [ REG_DI ], read16 ( source, r.general [ REG_SI ] ) );
						else
							write8 ( SEG_ES, r.general [ REG_DI ], read8 ( source, r.general [ REG_SI ] ) );

						r.general [ REG_SI ] = static_cast < uint16_t > ( r.general [ REG_SI ] + step );
						r.general [ REG_DI ] = static_cast < uint16_t > ( r.general [ REG_DI ] + step );
						break;

					case 0xA6:

						if ( word )
							subtract ( read16 ( source, r.general [ REG_SI ] ), read16 ( SEG_ES, r.general [ REG_DI ] ), 0, true );
						else
							subtract ( read8 ( source, r.general [ REG_SI ] ), read8 ( SEG_ES, r.general [ REG_DI ] ), 0, false );

						r.general [ REG_SI ] = static_cast < uint16_t > ( r.general [ REG_SI ] + step );
						r.general [ REG_DI ] = static_cast < uint16_t > ( r.general [ REG_DI ] + step );
						break;

					case 0xAA:

						if ( word )
							write16 ( SEG_ES, r.general [ REG_DI ], r.general [ REG_AX ] );
						else
							write8 ( SEG_ES, r.general [ REG_DI ], reg8 ( 0 ) );

						r.general [ REG_DI ] = static_cast < uint16_t > ( r.general [ REG_DI ] + step );
						break;

					case 0xAC:

						if ( word )
							r.general [ REG_AX ] = read16 ( source, r.general [ REG_SI ] );
						else
							reg8 ( 0 ) = read8 ( source, r.general [ REG_SI ] );

						r.general [ REG_SI ] = static_cast < uint16_t > ( r.general [ REG_SI ] + step );
						break;

					default:

						if ( word )
							subtract ( r.general [ REG_AX ], read16 ( SEG_ES, r.general [ REG_DI ] ), 0, true );
						else
							subtract ( reg8 ( 0 ), read8 ( SEG_ES, r.general [ REG_DI ] ), 0, false );

						r.general [ REG_DI ] = static_cast < uint16_t > ( r.general [ REG_DI ] + step );
						break;

				}

				iterations++;

				if ( !repeat )
					return;

				bool compares = kind == 0xA6 || kind == 0xAE;

				if ( !compares && interrupted ( ) ) {

					r.general [ REG_CX ] = static_cast < uint16_t > ( r.general [ REG_CX ] - 1 );

					return;

				}

				r.general [ REG_CX ] = static_cast < uint16_t > ( r.general [ REG_CX ] - 1 );

				if ( compares ) {

					if ( repeat == 0xF3 && !flag ( FLAG_ZERO ) )
						return;

					if ( repeat == 0xF2 && flag ( FLAG_ZERO ) )
						return;

					if ( interrupted ( ) )
						return;

				}

			}

		}

		bool interrupted ( ) {

			if ( !flag ( FLAG_TRAP ) && !core.interrupt_waiting ( ) )
				return false;

			r.ip = static_cast < uint16_t > ( r.ip - 2 );

			return true;

		}

		void execute ( ) {

			uint16_t start_segment = r.segments [ SEG_CS ];
			uint16_t start_offset = r.ip;

			dispatch ( );

			uint32_t clocks = timing ( );
			uint32_t queued = core.queue;
			uint32_t missing = fetched > queued ? fetched - queued : 0;

			clocks = base::max ( clocks, missing * 4 );

			if ( core.vectored || r.segments [ SEG_CS ] != start_segment || r.ip != static_cast < uint16_t > ( start_offset + fetched ) )
				core.queue = 0;
			else {

				uint32_t busy = ( transfers + missing ) * 4;
				uint32_t idle = clocks > busy ? clocks - busy : 0;

				core.queue = base::min ( ( queued > fetched ? queued - fetched : 0 ) + idle / 4, 4u );

			}

			core.cycles += clocks;

		}

		void dispatch ( ) {

			while ( true ) {

				opcode = fetch8 ( );

				switch ( opcode ) {

					case 0x26:
					case 0x2E:
					case 0x36:
					case 0x3E:
						segment_override = ( opcode >> 3 ) & 3;
						prefixes++;
						continue;

					case 0xF0:
					case 0xF1:
						prefixes++;
						continue;

					case 0xF2:
					case 0xF3:
						repeat = opcode;
						prefixes++;
						continue;

					default:
						break;

				}

				break;

			}

			if ( opcode < 0x40 && ( opcode & 7 ) < 6 ) {

				uint8_t operation = opcode >> 3;

				switch ( opcode & 7 ) {

					case 0: {

						decode_modrm ( );

						uint8_t result = static_cast < uint8_t > ( alu ( operation, get_rm8 ( ), reg8 ( reg ), false ) );

						if ( operation != 7 )
							set_rm8 ( result );

						return;

					}

					case 1: {

						decode_modrm ( );

						uint16_t result = alu ( operation, get_rm16 ( ), reg16 ( reg ), true );

						if ( operation != 7 )
							set_rm16 ( result );

						return;

					}

					case 2: {

						decode_modrm ( );

						uint8_t result = static_cast < uint8_t > ( alu ( operation, reg8 ( reg ), get_rm8 ( ), false ) );

						if ( operation != 7 )
							reg8 ( reg ) = result;

						return;

					}

					case 3: {

						decode_modrm ( );

						uint16_t result = alu ( operation, reg16 ( reg ), get_rm16 ( ), true );

						if ( operation != 7 )
							reg16 ( reg ) = result;

						return;

					}

					case 4: {

						uint8_t immediate = fetch8 ( );
						uint8_t result = static_cast < uint8_t > ( alu ( operation, reg8 ( 0 ), immediate, false ) );

						if ( operation != 7 )
							reg8 ( 0 ) = result;

						return;

					}

					default: {

						uint16_t immediate = fetch16 ( );
						uint16_t result = alu ( operation, r.general [ REG_AX ], immediate, true );

						if ( operation != 7 )
							r.general [ REG_AX ] = result;

						return;

					}

				}

			}

			switch ( opcode ) {

				case 0x06:
				case 0x0E:
				case 0x16:
				case 0x1E:
					push ( r.segments [ ( opcode >> 3 ) & 3 ] );
					return;

				case 0x07:
				case 0x0F:
				case 0x17:
				case 0x1F:
					r.segments [ ( opcode >> 3 ) & 3 ] = pop ( );
					core.inhibit = true;
					return;

				case 0x27:
				case 0x2F: {

					uint8_t old_al = reg8 ( 0 );
					uint8_t limit = flag ( FLAG_AUXILIARY ) ? 0x9F : 0x99;
					uint8_t correction = 0;

					if ( ( old_al & 0x0F ) > 9 || flag ( FLAG_AUXILIARY ) )
						correction |= 0x06;

					if ( old_al > limit || flag ( FLAG_CARRY ) )
						correction |= 0x60;

					if ( opcode == 0x2F )
						reg8 ( 0 ) = static_cast < uint8_t > ( subtract ( old_al, correction, 0, false ) );
					else
						reg8 ( 0 ) = static_cast < uint8_t > ( add ( old_al, correction, 0, false ) );

					set_flag ( FLAG_AUXILIARY, ( correction & 0x06 ) != 0 );
					set_flag ( FLAG_CARRY, ( correction & 0x60 ) != 0 );

					return;

				}

				case 0x37:
				case 0x3F: {

					bool subtracting = opcode == 0x3F;
					bool adjust = ( reg8 ( 0 ) & 0x0F ) > 9 || flag ( FLAG_AUXILIARY );
					uint8_t correction = adjust ? 6 : 0;
					uint16_t al = subtracting ? subtract ( reg8 ( 0 ), correction, 0, false ) : add ( reg8 ( 0 ), correction, 0, false );

					if ( adjust )
						reg8 ( 4 ) = static_cast < uint8_t > ( subtracting ? reg8 ( 4 ) - 1 : reg8 ( 4 ) + 1 );

					reg8 ( 0 ) = static_cast < uint8_t > ( al & 0x0F );
					set_flag ( FLAG_AUXILIARY, adjust );
					set_flag ( FLAG_CARRY, adjust );

					return;

				}

				default:
					break;

			}

			if ( opcode >= 0x40 && opcode <= 0x4F ) {

				uint8_t index = opcode & 7;

				r.general [ index ] = increment ( r.general [ index ], opcode < 0x48 ? 1 : -1, true );

				return;

			}

			if ( opcode >= 0x50 && opcode <= 0x57 ) {

				if ( opcode == 0x54 )
					push ( static_cast < uint16_t > ( r.general [ REG_SP ] - 2 ) );
				else
					push ( r.general [ opcode & 7 ] );

				return;

			}

			if ( opcode >= 0x58 && opcode <= 0x5F ) {

				uint16_t value = pop ( );

				r.general [ opcode & 7 ] = value;

				return;

			}

			if ( opcode >= 0x60 && opcode <= 0x7F ) {

				jump_short ( condition ( opcode & 0x0F ) );

				return;

			}

			if ( opcode >= 0x91 && opcode <= 0x97 ) {

				uint16_t held = r.general [ REG_AX ];

				r.general [ REG_AX ] = r.general [ opcode & 7 ];
				r.general [ opcode & 7 ] = held;

				return;

			}

			if ( opcode >= 0xB0 && opcode <= 0xB7 ) {

				reg8 ( opcode & 7 ) = fetch8 ( );

				return;

			}

			if ( opcode >= 0xB8 && opcode <= 0xBF ) {

				r.general [ opcode & 7 ] = fetch16 ( );

				return;

			}

			if ( opcode >= 0xD8 && opcode <= 0xDF ) {

				decode_modrm ( );

				if ( mod != 3 )
					read8 ( ea_segment, ea );

				return;

			}

			switch ( opcode ) {

				case 0x80:
				case 0x82: {

					decode_modrm ( );

					uint8_t value = get_rm8 ( );
					uint8_t immediate = fetch8 ( );
					uint8_t result = static_cast < uint8_t > ( alu ( reg, value, immediate, false ) );

					if ( reg != 7 )
						set_rm8 ( result );

					return;

				}

				case 0x81:
				case 0x83: {

					decode_modrm ( );

					uint16_t value = get_rm16 ( );
					uint16_t immediate = opcode == 0x81 ? fetch16 ( ) : static_cast < uint16_t > ( static_cast < int16_t > ( static_cast < int8_t > ( fetch8 ( ) ) ) );
					uint16_t result = alu ( reg, value, immediate, true );

					if ( reg != 7 )
						set_rm16 ( result );

					return;

				}

				case 0x84:
					decode_modrm ( );
					logic ( static_cast < uint16_t > ( get_rm8 ( ) & reg8 ( reg ) ), false );
					return;

				case 0x85:
					decode_modrm ( );
					logic ( static_cast < uint16_t > ( get_rm16 ( ) & reg16 ( reg ) ), true );
					return;

				case 0x86: {

					decode_modrm ( );

					uint8_t value = get_rm8 ( );

					set_rm8 ( reg8 ( reg ) );
					reg8 ( reg ) = value;

					return;

				}

				case 0x87: {

					decode_modrm ( );

					uint16_t value = get_rm16 ( );

					set_rm16 ( reg16 ( reg ) );
					reg16 ( reg ) = value;

					return;

				}

				case 0x88:
					decode_modrm ( );
					set_rm8 ( reg8 ( reg ) );
					return;

				case 0x89:
					decode_modrm ( );
					set_rm16 ( reg16 ( reg ) );
					return;

				case 0x8A:
					decode_modrm ( );
					reg8 ( reg ) = get_rm8 ( );
					return;

				case 0x8B:
					decode_modrm ( );
					reg16 ( reg ) = get_rm16 ( );
					return;

				case 0x8C:
					decode_modrm ( );
					set_rm16 ( r.segments [ reg & 3 ] );
					return;

				case 0x8D:
					decode_modrm ( );

					if ( mod != 3 )
						reg16 ( reg ) = ea;

					return;

				case 0x8E:
					decode_modrm ( );
					r.segments [ reg & 3 ] = get_rm16 ( );
					core.inhibit = true;
					return;

				case 0x8F: {

					uint16_t value = pop ( );

					decode_modrm ( );
					set_rm16 ( value );

					return;

				}

				case 0x90:
					return;

				case 0x98:
					r.general [ REG_AX ] = static_cast < uint16_t > ( static_cast < int16_t > ( static_cast < int8_t > ( reg8 ( 0 ) ) ) );
					return;

				case 0x99:
					r.general [ REG_DX ] = ( r.general [ REG_AX ] & 0x8000 ) ? 0xFFFF : 0x0000;
					return;

				case 0x9A: {

					uint16_t offset = fetch16 ( );
					uint16_t segment_value = fetch16 ( );

					push ( r.segments [ SEG_CS ] );
					push ( r.ip );
					r.segments [ SEG_CS ] = segment_value;
					r.ip = offset;

					return;

				}

				case 0x9B:
					return;

				case 0x9C:
					push ( r.flags );
					return;

				case 0x9D:
					load_flags ( pop ( ) );
					return;

				case 0x9E:
					r.flags = static_cast < uint16_t > ( ( r.flags & 0xFF00 ) | ( reg8 ( 4 ) & 0xD5 ) | 0x02 );
					return;

				case 0x9F:
					reg8 ( 4 ) = static_cast < uint8_t > ( r.flags );
					return;

				case 0xA0:
					reg8 ( 0 ) = read8 ( data_segment ( ), fetch16 ( ) );
					return;

				case 0xA1:
					r.general [ REG_AX ] = read16 ( data_segment ( ), fetch16 ( ) );
					return;

				case 0xA2:
					write8 ( data_segment ( ), fetch16 ( ), reg8 ( 0 ) );
					return;

				case 0xA3:
					write16 ( data_segment ( ), fetch16 ( ), r.general [ REG_AX ] );
					return;

				case 0xA4:
				case 0xA5:
				case 0xA6:
				case 0xA7:
				case 0xAA:
				case 0xAB:
				case 0xAC:
				case 0xAD:
				case 0xAE:
				case 0xAF:
					string_operation ( );
					return;

				case 0xA8:
					logic ( static_cast < uint16_t > ( reg8 ( 0 ) & fetch8 ( ) ), false );
					return;

				case 0xA9:
					logic ( static_cast < uint16_t > ( r.general [ REG_AX ] & fetch16 ( ) ), true );
					return;

				case 0xC0:
				case 0xC2: {

					uint16_t release = fetch16 ( );

					r.ip = pop ( );
					r.general [ REG_SP ] = static_cast < uint16_t > ( r.general [ REG_SP ] + release );

					return;

				}

				case 0xC1:
				case 0xC3:
					r.ip = pop ( );
					return;

				case 0xC4:
				case 0xC5: {

					decode_modrm ( );

					reg16 ( reg ) = read16 ( ea_segment, ea );
					r.segments [ opcode == 0xC4 ? SEG_ES : SEG_DS ] = read16 ( ea_segment, static_cast < uint16_t > ( ea + 2 ) );

					return;

				}

				case 0xC6:
					decode_modrm ( );
					set_rm8 ( fetch8 ( ) );
					return;

				case 0xC7:
					decode_modrm ( );
					set_rm16 ( fetch16 ( ) );
					return;

				case 0xC8:
				case 0xCA: {

					uint16_t release = fetch16 ( );

					r.ip = pop ( );
					r.segments [ SEG_CS ] = pop ( );
					r.general [ REG_SP ] = static_cast < uint16_t > ( r.general [ REG_SP ] + release );

					return;

				}

				case 0xC9:
				case 0xCB:
					r.ip = pop ( );
					r.segments [ SEG_CS ] = pop ( );
					return;

				case 0xCC:
					raise ( 3 );
					return;

				case 0xCD:
					raise ( fetch8 ( ) );
					return;

				case 0xCE:

					taken = flag ( FLAG_OVERFLOW );

					if ( taken )
						raise ( 4 );

					return;

				case 0xCF:
					r.ip = pop ( );
					r.segments [ SEG_CS ] = pop ( );
					load_flags ( pop ( ) );
					return;

				case 0xD0:
				case 0xD1:
				case 0xD2:
				case 0xD3: {

					decode_modrm ( );

					bool word = opcode & 1;
					uint8_t count = opcode >= 0xD2 ? reg8 ( 1 ) : 1;

					extra = opcode >= 0xD2 ? 4u * count : 0;

					if ( word )
						set_rm16 ( shift ( reg, get_rm16 ( ), count, true ) );
					else
						set_rm8 ( static_cast < uint8_t > ( shift ( reg, get_rm8 ( ), count, false ) ) );

					return;

				}

				case 0xD4: {

					division d = { };

					d.b = fetch8 ( );
					d.c = reg8 ( 0 );

					if ( !long_divide ( d, false ) ) {

						fault ( );

						return;

					}

					reg8 ( 4 ) = static_cast < uint8_t > ( ~d.c );
					reg8 ( 0 ) = static_cast < uint8_t > ( d.a );
					logic ( reg8 ( 0 ), false );

					return;

				}

				case 0xD5: {

					uint8_t base_value = fetch8 ( );
					uint8_t product = static_cast < uint8_t > ( reg8 ( 4 ) * base_value );
					uint8_t result = static_cast < uint8_t > ( add ( reg8 ( 0 ), product, 0, false ) );

					reg8 ( 0 ) = result;
					reg8 ( 4 ) = 0;

					return;

				}

				case 0xD6:
					reg8 ( 0 ) = flag ( FLAG_CARRY ) ? 0xFF : 0x00;
					return;

				case 0xD7:
					reg8 ( 0 ) = read8 ( data_segment ( ), static_cast < uint16_t > ( r.general [ REG_BX ] + reg8 ( 0 ) ) );
					return;

				case 0xE0:
				case 0xE1:
				case 0xE2: {

					int8_t displacement = static_cast < int8_t > ( fetch8 ( ) );

					r.general [ REG_CX ] = static_cast < uint16_t > ( r.general [ REG_CX ] - 1 );

					taken = r.general [ REG_CX ] != 0;

					if ( opcode == 0xE0 )
						taken = taken && !flag ( FLAG_ZERO );
					else if ( opcode == 0xE1 )
						taken = taken && flag ( FLAG_ZERO );

					if ( taken )
						r.ip = static_cast < uint16_t > ( r.ip + displacement );

					return;

				}

				case 0xE3:
					jump_short ( r.general [ REG_CX ] == 0 );
					return;

				case 0xE4:
					reg8 ( 0 ) = memory.in8 ( fetch8 ( ) );
					return;

				case 0xE5: {

					uint8_t port = fetch8 ( );
					uint16_t low = memory.in8 ( port );

					r.general [ REG_AX ] = static_cast < uint16_t > ( low | memory.in8 ( static_cast < uint16_t > ( port + 1 ) ) << 8 );

					return;

				}

				case 0xE6:
					memory.out8 ( fetch8 ( ), reg8 ( 0 ) );
					return;

				case 0xE7: {

					uint8_t port = fetch8 ( );

					memory.out8 ( port, reg8 ( 0 ) );
					memory.out8 ( static_cast < uint16_t > ( port + 1 ), reg8 ( 4 ) );

					return;

				}

				case 0xE8: {

					uint16_t displacement = fetch16 ( );

					push ( r.ip );
					r.ip = static_cast < uint16_t > ( r.ip + displacement );

					return;

				}

				case 0xE9: {

					uint16_t displacement = fetch16 ( );

					r.ip = static_cast < uint16_t > ( r.ip + displacement );

					return;

				}

				case 0xEA: {

					uint16_t offset = fetch16 ( );
					uint16_t segment_value = fetch16 ( );

					r.segments [ SEG_CS ] = segment_value;
					r.ip = offset;

					return;

				}

				case 0xEB:
					jump_short ( true );
					return;

				case 0xEC:
					reg8 ( 0 ) = memory.in8 ( r.general [ REG_DX ] );
					return;

				case 0xED: {

					uint16_t low = memory.in8 ( r.general [ REG_DX ] );

					r.general [ REG_AX ] = static_cast < uint16_t > ( low | memory.in8 ( static_cast < uint16_t > ( r.general [ REG_DX ] + 1 ) ) << 8 );

					return;

				}

				case 0xEE:
					memory.out8 ( r.general [ REG_DX ], reg8 ( 0 ) );
					return;

				case 0xEF:
					memory.out8 ( r.general [ REG_DX ], reg8 ( 0 ) );
					memory.out8 ( static_cast < uint16_t > ( r.general [ REG_DX ] + 1 ), reg8 ( 4 ) );
					return;

				case 0xF4:
					core.halted = true;
					return;

				case 0xF5:
					set_flag ( FLAG_CARRY, !flag ( FLAG_CARRY ) );
					return;

				case 0xF6:
				case 0xF7: {

					decode_modrm ( );

					bool word = opcode & 1;

					switch ( reg ) {

						case 0:
						case 1:

							if ( word )
								logic ( static_cast < uint16_t > ( get_rm16 ( ) & fetch16 ( ) ), true );
							else
								logic ( static_cast < uint16_t > ( get_rm8 ( ) & fetch8 ( ) ), false );

							return;

						case 2:

							if ( word )
								set_rm16 ( static_cast < uint16_t > ( ~get_rm16 ( ) ) );
							else
								set_rm8 ( static_cast < uint8_t > ( ~get_rm8 ( ) ) );

							return;

						case 3:

							if ( word )
								set_rm16 ( subtract ( 0, get_rm16 ( ), 0, true ) );
							else
								set_rm8 ( static_cast < uint8_t > ( subtract ( 0, get_rm8 ( ), 0, false ) ) );

							return;

						case 4:
						case 5:
							multiply ( word, reg == 5 );
							return;

						default:
							divide ( word, reg == 7 );
							return;

					}

				}

				case 0xF8:
					set_flag ( FLAG_CARRY, false );
					return;

				case 0xF9:
					set_flag ( FLAG_CARRY, true );
					return;

				case 0xFA:
					set_flag ( FLAG_INTERRUPT, false );
					return;

				case 0xFB:
					load_flags ( static_cast < uint16_t > ( r.flags | FLAG_INTERRUPT ) );
					return;

				case 0xFC:
					set_flag ( FLAG_DIRECTION, false );
					return;

				case 0xFD:
					set_flag ( FLAG_DIRECTION, true );
					return;

				case 0xFE: {

					decode_modrm ( );

					if ( reg < 2 )
						set_rm8 ( static_cast < uint8_t > ( increment ( get_rm8 ( ), reg == 0 ? 1 : -1, false ) ) );

					return;

				}

				case 0xFF: {

					decode_modrm ( );

					switch ( reg ) {

						case 0:
						case 1:
							set_rm16 ( increment ( get_rm16 ( ), reg == 0 ? 1 : -1, true ) );
							return;

						case 2: {

							uint16_t target = get_rm16 ( );

							push ( r.ip );
							r.ip = target;

							return;

						}

						case 3: {

							uint16_t offset = read16 ( ea_segment, ea );
							uint16_t segment_value = read16 ( ea_segment, static_cast < uint16_t > ( ea + 2 ) );

							push ( r.segments [ SEG_CS ] );
							push ( r.ip );
							r.segments [ SEG_CS ] = segment_value;
							r.ip = offset;

							return;

						}

						case 4:
							r.ip = get_rm16 ( );
							return;

						case 5: {

							uint16_t offset = read16 ( ea_segment, ea );
							uint16_t segment_value = read16 ( ea_segment, static_cast < uint16_t > ( ea + 2 ) );

							r.segments [ SEG_CS ] = segment_value;
							r.ip = offset;

							return;

						}

						default: {

							uint16_t value = get_rm16 ( );

							if ( mod == 3 && rm == REG_SP )
								value = static_cast < uint16_t > ( value - 2 );

							push ( value );

							return;

						}

					}

				}

				default:
					return;

			}

		}

		uint32_t timing ( ) const {

			bool memory_form = decoded && mod != 3;
			uint32_t ea_cost = memory_form ? ea_clocks : 0;
			uint32_t w = ( opcode & 1 ) ? 4 : 0;
			uint32_t clocks = prefixes * 2 + ( faulted ? 71 : 0 );

			if ( opcode < 0x40 ) {

				switch ( opcode & 7 ) {

					case 0:
					case 1:

						if ( !memory_form )
							return clocks + 3;

						return clocks + ea_cost + ( ( opcode >> 3 ) == 7 ? 9 + w : 16 + w * 2 );

					case 2:
					case 3:
						return clocks + ( memory_form ? 9 + w + ea_cost : 3 );

					case 4:
					case 5:
						return clocks + 4;

					case 6:
						return clocks + 14;

					default:
						return clocks + ( opcode & 0x20 ? ( opcode & 0x10 ? 8 : 4 ) : 12 );

				}

			}

			if ( opcode < 0x50 )
				return clocks + 2;

			if ( opcode < 0x58 )
				return clocks + 15;

			if ( opcode < 0x60 )
				return clocks + 12;

			if ( opcode < 0x80 )
				return clocks + ( taken ? 17 : 4 );

			if ( opcode >= 0x90 && opcode < 0x98 )
				return clocks + 3;

			if ( opcode >= 0xB0 && opcode < 0xC0 )
				return clocks + 4;

			if ( opcode >= 0xD8 && opcode < 0xE0 )
				return clocks + ( memory_form ? 14 + ea_cost : 2 );

			switch ( opcode ) {

				case 0x80:
				case 0x81:
				case 0x82:
				case 0x83:

					if ( !memory_form )
						return clocks + 4;

					return clocks + ea_cost + ( reg == 7 ? 10 + w : 17 + w * 2 );

				case 0x84:
				case 0x85:
					return clocks + ( memory_form ? 9 + w + ea_cost : 3 );

				case 0x86:
				case 0x87:
					return clocks + ( memory_form ? 17 + w * 2 + ea_cost : 4 );

				case 0x88:
				case 0x89:
					return clocks + ( memory_form ? 9 + w + ea_cost : 2 );

				case 0x8A:
				case 0x8B:
					return clocks + ( memory_form ? 8 + w + ea_cost : 2 );

				case 0x8C:
					return clocks + ( memory_form ? 13 + ea_cost : 2 );

				case 0x8D:
					return clocks + 2 + ea_cost;

				case 0x8E:
					return clocks + ( memory_form ? 12 + ea_cost : 2 );

				case 0x8F:
					return clocks + ( memory_form ? 25 + ea_cost : 12 );

				case 0x98:
				case 0x9F:
					return clocks + 2;

				case 0x9E:
					return clocks + 4;

				case 0x99:
					return clocks + 5;

				case 0x9A:
					return clocks + 39;

				case 0x9B:
					return clocks + 3;

				case 0x9C:
					return clocks + 14;

				case 0x9D:
					return clocks + 12;

				case 0xA0:
				case 0xA1:
					return clocks + 10 + w;

				case 0xA2:
				case 0xA3:
					return clocks + 11 + w;

				case 0xA4:
				case 0xA5:
					return clocks + ( repeat ? 9 + ( 17 + w * 2 ) * iterations : 18 + w * 2 );

				case 0xA6:
				case 0xA7:
					return clocks + ( repeat ? 9 + ( 22 + w * 2 ) * iterations : 22 + w * 2 );

				case 0xA8:
				case 0xA9:
					return clocks + 4;

				case 0xAA:
				case 0xAB:
					return clocks + ( repeat ? 9 + ( 10 + w ) * iterations : 11 + w );

				case 0xAC:
				case 0xAD:
					return clocks + ( repeat ? 9 + ( 13 + w ) * iterations : 12 + w );

				case 0xAE:
				case 0xAF:
					return clocks + ( repeat ? 9 + ( 15 + w ) * iterations : 15 + w );

				case 0xC0:
				case 0xC2:
					return clocks + 25;

				case 0xC1:
				case 0xC3:
					return clocks + 20;

				case 0xC4:
				case 0xC5:
					return clocks + 26 + ea_cost;

				case 0xC6:
				case 0xC7:
					return clocks + ( memory_form ? 10 + w + ea_cost : 4 );

				case 0xC8:
				case 0xCA:
					return clocks + 36;

				case 0xC9:
				case 0xCB:
					return clocks + 34;

				case 0xCC:
					return clocks + 71;

				case 0xCD:
					return clocks + 70;

				case 0xCE:
					return clocks + ( taken ? 73 : 4 );

				case 0xCF:
					return clocks + 44;

				case 0xD0:
				case 0xD1:
					return clocks + ( memory_form ? 15 + w * 2 + ea_cost : 2 );

				case 0xD2:
				case 0xD3:
					return clocks + extra + ( memory_form ? 20 + w * 2 + ea_cost : 8 );

				case 0xD4:
					return clocks + ( faulted ? extra : 83 );

				case 0xD5:
					return clocks + 60;

				case 0xD6:
					return clocks + 4;

				case 0xD7:
					return clocks + 12;

				case 0xE0:
					return clocks + ( taken ? 19 : 5 );

				case 0xE1:
				case 0xE3:
					return clocks + ( taken ? 18 : 6 );

				case 0xE2:
					return clocks + ( taken ? 17 : 5 );

				case 0xE4:
				case 0xE5:
				case 0xE6:
				case 0xE7:
					return clocks + 10 + w;

				case 0xE8:
					return clocks + 23;

				case 0xE9:
				case 0xEB:
					return clocks + 17;

				case 0xEA:
					return clocks + 19;

				case 0xEC:
				case 0xED:
				case 0xEE:
				case 0xEF:
					return clocks + 8 + w;

				case 0xF6:
				case 0xF7:

					if ( reg < 2 )
						return clocks + ( memory_form ? 11 + w + ea_cost : 5 );

					if ( reg < 4 )
						return clocks + ( memory_form ? 16 + w * 2 + ea_cost : 3 );

					return clocks + extra + ( memory_form ? 6 + w + ea_cost : 0 );

				case 0xFE:
					return clocks + ( memory_form ? 15 + ea_cost : 3 );

				case 0xFF:

					switch ( reg ) {

						case 0:
						case 1:
							return clocks + ( memory_form ? 23 + ea_cost : 2 );

						case 2:
							return clocks + ( memory_form ? 32 + ea_cost : 23 );

						case 3:
							return clocks + 56 + ea_cost;

						case 4:
							return clocks + ( memory_form ? 24 + ea_cost : 14 );

						case 5:
							return clocks + 34 + ea_cost;

						default:
							return clocks + ( memory_form ? 24 + ea_cost : 15 );

					}

				default:
					return clocks + 2;

			}

		}

	};

	void cpu::step ( ) {

		bool trap = false;

		if ( !halted ) {

			runner work ( *this );

			trap = ( state.flags & FLAG_TRAP ) != 0;
			inhibit = false;
			vectored = false;
			work.execute ( );
			instructions++;
			trap = trap && !vectored;

		}

		if ( nmi ) {

			nmi = false;
			interrupt ( 2 );
			cycles += 70;

			return;

		}

		if ( !inhibit && ( state.flags & FLAG_INTERRUPT ) && controller && controller->pending ( ) ) {

			interrupt ( controller->acknowledge ( ) );
			cycles += 81;

			return;

		}

		if ( trap ) {

			interrupt ( 1 );
			cycles += 70;

			return;

		}

		if ( halted )
			cycles += 4;

	}

}