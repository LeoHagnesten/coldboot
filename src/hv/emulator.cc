#include "hv/emulator.hh"
#include "hv/paging.hh"

namespace hv {

	static constexpr uint64_t string_batch = 4096;

	static uint64_t mask_of ( uint32_t size ) {

		return size >= 8 ? ~0ull : ( 1ull << ( size * 8 ) ) - 1;

	}

	static uint64_t sign_extend ( uint64_t value, uint32_t size ) {

		if ( size >= 8 )
			return value;

		uint64_t sign = 1ull << ( size * 8 - 1 );

		return ( ( value & mask_of ( size ) ) ^ sign ) - sign;

	}

	static uint64_t merge ( uint64_t old, uint64_t value, uint32_t size ) {

		if ( size == 1 )
			return ( old & ~0xFFull ) | ( value & 0xFF );

		if ( size == 2 )
			return ( old & ~0xFFFFull ) | ( value & 0xFFFF );

		return size == 4 ? value & 0xFFFFFFFF : value;

	}

	static bool parity ( uint64_t value ) {

		uint8_t bits = static_cast < uint8_t > ( value );

		bits ^= bits >> 4;
		bits ^= bits >> 2;
		bits ^= bits >> 1;

		return !( bits & 1 );

	}

	class executor {

	public:

		executor ( cpu_state& state, guest_bus& target, const uint8_t* code, uint32_t size ) : cpu ( state ), bus ( target ), bytes ( code ), count ( size ) { }

		emulate_result run ( );

	private:

		uint8_t next ( ) {

			if ( at >= count ) {

				truncated = true;

				return 0;

			}

			return bytes [ at++ ];

		}

		uint64_t take ( uint32_t size ) {

			uint64_t value = 0;

			for ( uint32_t i = 0; i < size; i++ )
				value |= static_cast < uint64_t > ( next ( ) ) << ( i * 8 );

			return value;

		}

		uint64_t reg_get ( uint32_t index, uint32_t size ) const {

			if ( size == 1 && !rex && index >= 4 && index < 8 )
				return cpu.gpr [ index - 4 ] >> 8 & 0xFF;

			return cpu.gpr [ index ] & mask_of ( size );

		}

		void reg_set ( uint32_t index, uint32_t size, uint64_t value ) {

			if ( size == 1 && !rex && index >= 4 && index < 8 ) {

				uint64_t& target = cpu.gpr [ index - 4 ];

				target = ( target & ~0xFF00ull ) | ( value & 0xFF ) << 8;

				return;

			}

			cpu.gpr [ index ] = merge ( cpu.gpr [ index ], value, size );

		}

		uint64_t linear ( int32_t segment, uint64_t offset ) const {

			offset &= mask_of ( address );

			if ( cpu.code64 ( ) )
				return segment == SEG_FS || segment == SEG_GS ? cpu.segments [ segment ].base + offset : offset;

			return ( cpu.segments [ segment ].base + offset ) & 0xFFFFFFFF;

		}

		uint64_t operand_address ( ) const {

			return linear ( segment_override >= 0 ? segment_override : default_segment, offset );

		}

		bool probe ( uint64_t where, uint32_t size, bool write, uint64_t* physical ) {

			uint32_t room = static_cast < uint32_t > ( 0x1000 - ( where & 0xFFF ) );

			if ( !translate ( cpu, bus, where, write, physical [ 0 ] ) )
				return false;

			return size <= room || translate ( cpu, bus, where + room, write, physical [ 1 ] );

		}

		bool access ( uint64_t where, uint8_t* data, uint32_t size, bool write ) {

			uint64_t physical [ 2 ] = { };
			uint32_t room = static_cast < uint32_t > ( 0x1000 - ( where & 0xFFF ) );
			uint32_t first = size < room ? size : room;

			if ( !probe ( where, size, write, physical ) )
				return false;

			if ( write ) {

				bus.write ( physical [ 0 ], data, first );

				if ( first < size )
					bus.write ( physical [ 1 ], data + first, size - first );

			} else {

				bus.read ( physical [ 0 ], data, first );

				if ( first < size )
					bus.read ( physical [ 1 ], data + first, size - first );

			}

			return true;

		}

		bool load ( uint64_t where, uint32_t size, uint64_t& value ) {

			uint8_t data [ 8 ] = { };

			if ( !access ( where, data, size, false ) )
				return false;

			value = 0;

			for ( int32_t i = static_cast < int32_t > ( size ) - 1; i >= 0; i-- )
				value = value << 8 | data [ i ];

			return true;

		}

		bool store ( uint64_t where, uint32_t size, uint64_t value ) {

			uint8_t data [ 8 ];

			for ( uint32_t i = 0; i < size; i++ )
				data [ i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

			return access ( where, data, size, true );

		}

		bool read_rm ( uint32_t size, uint64_t& value ) {

			if ( !memory ) {

				value = reg_get ( rm, size );

				return true;

			}

			return load ( operand_address ( ), size, value );

		}

		bool write_rm ( uint32_t size, uint64_t value ) {

			if ( !memory ) {

				reg_set ( rm, size, value );

				return true;

			}

			return store ( operand_address ( ), size, value );

		}

		void modrm ( );

		void seal ( ) {

			length = at;

			if ( relative )
				offset += cpu.rip + length;

		}

		void finish ( ) {

			uint32_t bits = cpu.code_bits ( );

			cpu.rip = ( cpu.rip + length ) & ( bits == 64 ? ~0ull : ( bits == 32 ? 0xFFFFFFFFull : 0xFFFFull ) );

		}

		uint64_t alu ( uint32_t operation, uint64_t a, uint64_t b, uint32_t size );

		emulate_result string ( );

		emulate_result group ( uint32_t size, uint32_t immediate_size, bool sign );

		emulate_result unary ( uint32_t size );

		cpu_state& cpu;
		guest_bus& bus;
		const uint8_t* bytes;
		uint32_t count;
		uint32_t at = 0;
		uint32_t length = 0;
		bool truncated = false;
		uint32_t operand = 4;
		uint32_t address = 4;
		int32_t segment_override = -1;
		int32_t default_segment = SEG_DS;
		bool rep = false;
		bool repne = false;
		bool rex = false;
		bool wide = false;
		uint8_t rex_r = 0;
		uint8_t rex_x = 0;
		uint8_t rex_b = 0;
		uint8_t opcode = 0;
		uint8_t mod = 0;
		uint8_t reg = 0;
		uint8_t rm = 0;
		bool memory = false;
		bool relative = false;
		uint64_t offset = 0;

	};

	void executor::modrm ( ) {

		uint8_t value = next ( );

		mod = value >> 6;
		reg = static_cast < uint8_t > ( ( value >> 3 & 7 ) | rex_r );
		rm = value & 7;

		if ( mod == 3 ) {

			rm |= rex_b;
			memory = false;

			return;

		}

		memory = true;

		if ( address == 2 ) {

			static const uint8_t first [ 8 ] = { REG_BX, REG_BX, REG_BP, REG_BP, REG_SI, REG_DI, REG_BP, REG_BX };
			static const uint8_t second [ 8 ] = { REG_SI, REG_DI, REG_SI, REG_DI, 0xFF, 0xFF, 0xFF, 0xFF };

			if ( mod == 0 && rm == 6 ) {

				offset = take ( 2 );

				return;

			}

			offset = cpu.gpr [ first [ rm ] ];

			if ( second [ rm ] != 0xFF )
				offset += cpu.gpr [ second [ rm ] ];

			if ( first [ rm ] == REG_BP )
				default_segment = SEG_SS;

			if ( mod == 1 )
				offset += sign_extend ( take ( 1 ), 1 );
			else if ( mod == 2 )
				offset += take ( 2 );

			return;

		}

		offset = 0;

		if ( rm == 4 ) {

			uint8_t sib = next ( );
			uint8_t index = static_cast < uint8_t > ( ( sib >> 3 & 7 ) | rex_x );
			uint8_t base = sib & 7;

			if ( index != 4 )
				offset += cpu.gpr [ index ] << ( sib >> 6 );

			if ( base == 5 && mod == 0 )
				offset += sign_extend ( take ( 4 ), 4 );
			else {

				uint8_t full = static_cast < uint8_t > ( base | rex_b );

				offset += cpu.gpr [ full ];

				if ( full == REG_SP || full == REG_BP )
					default_segment = SEG_SS;

			}

		} else if ( rm == 5 && mod == 0 ) {

			offset = sign_extend ( take ( 4 ), 4 );
			relative = cpu.code64 ( );

		} else {

			uint8_t full = static_cast < uint8_t > ( rm | rex_b );

			offset = cpu.gpr [ full ];

			if ( full == REG_BP )
				default_segment = SEG_SS;

		}

		if ( mod == 1 )
			offset += sign_extend ( take ( 1 ), 1 );
		else if ( mod == 2 )
			offset += sign_extend ( take ( 4 ), 4 );

	}

	uint64_t executor::alu ( uint32_t operation, uint64_t a, uint64_t b, uint32_t size ) {

		uint64_t mask = mask_of ( size );
		uint32_t top = size * 8 - 1;
		uint64_t carry = cpu.rflags & FLAG_CF ? 1 : 0;
		uint64_t result = 0;
		uint64_t flags = 0;

		a &= mask;
		b &= mask;

		switch ( operation ) {

			case 0:
			case 2: {

				uint64_t extra = operation == 2 ? carry : 0;

				result = ( a + b + extra ) & mask;
				flags |= ( extra ? result <= a : result < a ) ? FLAG_CF : 0;
				flags |= ( ~( a ^ b ) & ( a ^ result ) ) >> top & 1 ? FLAG_OF : 0;
				flags |= ( a ^ b ^ result ) & 0x10 ? FLAG_AF : 0;

				break;

			}

			case 3:
			case 5:
			case 7: {

				uint64_t extra = operation == 3 ? carry : 0;

				result = ( a - b - extra ) & mask;
				flags |= ( extra ? a <= b : a < b ) ? FLAG_CF : 0;
				flags |= ( ( a ^ b ) & ( a ^ result ) ) >> top & 1 ? FLAG_OF : 0;
				flags |= ( a ^ b ^ result ) & 0x10 ? FLAG_AF : 0;

				break;

			}

			case 1:
				result = a | b;
				break;

			case 4:
				result = a & b;
				break;

			default:
				result = a ^ b;
				break;

		}

		flags |= result == 0 ? FLAG_ZF : 0;
		flags |= result >> top & 1 ? FLAG_SF : 0;
		flags |= parity ( result ) ? FLAG_PF : 0;
		cpu.rflags = ( cpu.rflags & ~FLAG_ARITHMETIC ) | flags;

		return result;

	}

	emulate_result executor::group ( uint32_t size, uint32_t immediate_size, bool sign ) {

		modrm ( );

		uint64_t immediate = take ( immediate_size );

		if ( sign )
			immediate = sign_extend ( immediate, immediate_size );

		seal ( );

		uint32_t operation = reg & 7;
		uint64_t value = 0;

		if ( truncated )
			return EMULATE_UNSUPPORTED;

		if ( !read_rm ( size, value ) )
			return EMULATE_FAULT;

		uint64_t result = alu ( operation, value, immediate, size );

		if ( operation != 7 && !write_rm ( size, result ) )
			return EMULATE_FAULT;

		finish ( );

		return EMULATE_DONE;

	}

	emulate_result executor::unary ( uint32_t size ) {

		modrm ( );

		uint32_t operation = reg & 7;
		uint64_t immediate = 0;

		if ( ( opcode == 0xF6 || opcode == 0xF7 ) && operation < 2 ) {

			uint32_t immediate_size = size > 4 ? 4 : size;

			immediate = sign_extend ( take ( immediate_size ), immediate_size );

		}

		seal ( );

		uint64_t value = 0;

		if ( truncated )
			return EMULATE_UNSUPPORTED;

		bool counting = opcode == 0xFE || opcode == 0xFF;

		if ( ( counting && operation > 1 ) || ( !counting && operation > 3 ) )
			return EMULATE_UNSUPPORTED;

		if ( !read_rm ( size, value ) )
			return EMULATE_FAULT;

		if ( counting ) {

			uint64_t keep = cpu.rflags & FLAG_CF;
			uint64_t result = alu ( operation ? 5 : 0, value, 1, size );

			cpu.rflags = ( cpu.rflags & ~FLAG_CF ) | keep;

			if ( !write_rm ( size, result ) )
				return EMULATE_FAULT;

		} else if ( operation < 2 )
			alu ( 4, value, immediate, size );
		else if ( operation == 2 ) {

			if ( !write_rm ( size, ~value ) )
				return EMULATE_FAULT;

		} else if ( !write_rm ( size, alu ( 5, 0, value, size ) ) )
			return EMULATE_FAULT;

		finish ( );

		return EMULATE_DONE;

	}

	emulate_result executor::string ( ) {

		bool ports = opcode >= 0x6C && opcode <= 0x6F;
		uint32_t size = opcode & 1 ? ( ports && operand > 4 ? 4 : operand ) : 1;
		bool repeat = rep || repne;
		uint64_t mask = mask_of ( address );
		uint64_t step = cpu.rflags & FLAG_DF ? 0 - static_cast < uint64_t > ( size ) : size;
		uint64_t remaining = repeat ? cpu.gpr [ REG_CX ] & mask : 1;
		int32_t source = segment_override >= 0 ? segment_override : SEG_DS;
		uint16_t port = static_cast < uint16_t > ( cpu.gpr [ REG_DX ] );
		bool compare = opcode == 0xA6 || opcode == 0xA7 || opcode == 0xAE || opcode == 0xAF;
		bool uses_source = opcode == 0xA4 || opcode == 0xA5 || opcode == 0xA6 || opcode == 0xA7 || opcode == 0xAC || opcode == 0xAD || opcode == 0x6E || opcode == 0x6F;
		bool uses_target = !( opcode == 0xAC || opcode == 0xAD || opcode == 0x6E || opcode == 0x6F );
		uint64_t done = 0;

		seal ( );

		if ( truncated )
			return EMULATE_UNSUPPORTED;

		while ( remaining && done < string_batch ) {

			uint64_t from = linear ( source, cpu.gpr [ REG_SI ] );
			uint64_t to = linear ( SEG_ES, cpu.gpr [ REG_DI ] );
			uint64_t value = 0;
			uint64_t other = 0;
			uint64_t physical [ 2 ] = { };
			bool writes = opcode == 0xA4 || opcode == 0xA5 || opcode == 0xAA || opcode == 0xAB || opcode == 0x6C || opcode == 0x6D;

			if ( writes && !probe ( to, size, true, physical ) )
				return EMULATE_FAULT;

			switch ( opcode ) {

				case 0xA4:
				case 0xA5:

					if ( !load ( from, size, value ) || !store ( to, size, value ) )
						return EMULATE_FAULT;

					break;

				case 0xAA:
				case 0xAB:

					if ( !store ( to, size, cpu.gpr [ REG_AX ] ) )
						return EMULATE_FAULT;

					break;

				case 0xAC:
				case 0xAD:

					if ( !load ( from, size, value ) )
						return EMULATE_FAULT;

					reg_set ( REG_AX, size, value );

					break;

				case 0xA6:
				case 0xA7:

					if ( !load ( from, size, value ) || !load ( to, size, other ) )
						return EMULATE_FAULT;

					alu ( 7, value, other, size );

					break;

				case 0xAE:
				case 0xAF:

					if ( !load ( to, size, other ) )
						return EMULATE_FAULT;

					alu ( 7, cpu.gpr [ REG_AX ], other, size );

					break;

				case 0x6C:
				case 0x6D:

					if ( !store ( to, size, bus.in ( port, size ) ) )
						return EMULATE_FAULT;

					break;

				default:

					if ( !load ( from, size, value ) )
						return EMULATE_FAULT;

					bus.out ( port, size, static_cast < uint32_t > ( value ) );

					break;

			}

			if ( uses_source )
				cpu.gpr [ REG_SI ] = merge ( cpu.gpr [ REG_SI ], ( cpu.gpr [ REG_SI ] + step ) & mask, address );

			if ( uses_target )
				cpu.gpr [ REG_DI ] = merge ( cpu.gpr [ REG_DI ], ( cpu.gpr [ REG_DI ] + step ) & mask, address );

			done++;

			if ( !repeat ) {

				remaining = 0;

				break;

			}

			remaining--;
			cpu.gpr [ REG_CX ] = merge ( cpu.gpr [ REG_CX ], remaining, address );

			if ( compare ) {

				bool zero = ( cpu.rflags & FLAG_ZF ) != 0;

				if ( ( rep && !zero ) || ( repne && zero ) )
					remaining = 0;

			}

		}

		if ( !remaining )
			finish ( );

		return EMULATE_DONE;

	}

	emulate_result executor::run ( ) {

		uint32_t bits = cpu.code_bits ( );
		bool operand_prefix = false;
		bool address_prefix = false;

		for ( ;; ) {

			uint8_t value = next ( );

			if ( truncated )
				return EMULATE_UNSUPPORTED;

			if ( bits == 64 && ( value & 0xF0 ) == 0x40 ) {

				rex = true;
				wide = ( value & 8 ) != 0;
				rex_r = static_cast < uint8_t > ( ( value >> 2 & 1 ) << 3 );
				rex_x = static_cast < uint8_t > ( ( value >> 1 & 1 ) << 3 );
				rex_b = static_cast < uint8_t > ( ( value & 1 ) << 3 );

				continue;

			}

			bool prefix = true;

			if ( value == 0x66 )
				operand_prefix = true;
			else if ( value == 0x67 )
				address_prefix = true;
			else if ( value == 0xF2 ) {

				repne = true;
				rep = false;

			} else if ( value == 0xF3 ) {

				rep = true;
				repne = false;

			} else if ( value == 0x26 || value == 0x2E || value == 0x36 || value == 0x3E )
				segment_override = value >> 3 & 3;
			else if ( value == 0x64 )
				segment_override = SEG_FS;
			else if ( value == 0x65 )
				segment_override = SEG_GS;
			else if ( value != 0xF0 )
				prefix = false;

			if ( !prefix ) {

				opcode = value;

				break;

			}

			rex = false;
			wide = false;
			rex_r = 0;
			rex_x = 0;
			rex_b = 0;

		}

		if ( bits == 64 ) {

			operand = wide ? 8 : ( operand_prefix ? 2 : 4 );
			address = address_prefix ? 4 : 8;

		} else if ( bits == 32 ) {

			operand = operand_prefix ? 2 : 4;
			address = address_prefix ? 2 : 4;

		} else {

			operand = operand_prefix ? 4 : 2;
			address = address_prefix ? 4 : 2;

		}

		uint32_t immediate_size = operand > 4 ? 4 : operand;

		if ( opcode == 0x0F ) {

			uint8_t second = next ( );

			if ( second == 0xB6 || second == 0xB7 || second == 0xBE || second == 0xBF ) {

				uint32_t source_size = second & 1 ? 2 : 1;
				uint64_t value = 0;

				modrm ( );
				seal ( );

				if ( truncated )
					return EMULATE_UNSUPPORTED;

				if ( !read_rm ( source_size, value ) )
					return EMULATE_FAULT;

				reg_set ( reg, operand, second >= 0xBE ? sign_extend ( value, source_size ) : value );
				finish ( );

				return EMULATE_DONE;

			}

			if ( second == 0xC3 ) {

				modrm ( );
				seal ( );

				if ( truncated || !memory )
					return EMULATE_UNSUPPORTED;

				if ( !write_rm ( operand, reg_get ( reg, operand ) ) )
					return EMULATE_FAULT;

				finish ( );

				return EMULATE_DONE;

			}

			return EMULATE_UNSUPPORTED;

		}

		if ( opcode < 0x40 && ( opcode & 7 ) < 6 ) {

			uint32_t operation = opcode >> 3;
			uint32_t form = opcode & 7;
			uint32_t size = form & 1 ? operand : 1;

			if ( form >= 4 ) {

				uint64_t immediate = take ( form == 4 ? 1 : immediate_size );

				seal ( );

				if ( truncated )
					return EMULATE_UNSUPPORTED;

				uint64_t result = alu ( operation, reg_get ( REG_AX, size ), form == 4 ? immediate : sign_extend ( immediate, immediate_size ), size );

				if ( operation != 7 )
					reg_set ( REG_AX, size, result );

				finish ( );

				return EMULATE_DONE;

			}

			modrm ( );
			seal ( );

			uint64_t value = 0;

			if ( truncated )
				return EMULATE_UNSUPPORTED;

			if ( !read_rm ( size, value ) )
				return EMULATE_FAULT;

			if ( form < 2 ) {

				uint64_t result = alu ( operation, value, reg_get ( reg, size ), size );

				if ( operation != 7 && !write_rm ( size, result ) )
					return EMULATE_FAULT;

			} else {

				uint64_t result = alu ( operation, reg_get ( reg, size ), value, size );

				if ( operation != 7 )
					reg_set ( reg, size, result );

			}

			finish ( );

			return EMULATE_DONE;

		}

		switch ( opcode ) {

			case 0x63: {

				if ( bits != 64 )
					return EMULATE_UNSUPPORTED;

				uint64_t value = 0;

				modrm ( );
				seal ( );

				if ( truncated )
					return EMULATE_UNSUPPORTED;

				if ( !read_rm ( 4, value ) )
					return EMULATE_FAULT;

				reg_set ( reg, operand, operand == 8 ? sign_extend ( value, 4 ) : value );
				finish ( );

				return EMULATE_DONE;

			}

			case 0x80:
				return group ( 1, 1, false );

			case 0x81:
				return group ( operand, immediate_size, true );

			case 0x83:
				return group ( operand, 1, true );

			case 0x84:
			case 0x85:
			case 0x86:
			case 0x87:
			case 0x88:
			case 0x89:
			case 0x8A:
			case 0x8B: {

				uint32_t size = opcode & 1 ? operand : 1;
				uint64_t value = 0;

				modrm ( );
				seal ( );

				if ( truncated )
					return EMULATE_UNSUPPORTED;

				if ( opcode == 0x88 || opcode == 0x89 ) {

					if ( !write_rm ( size, reg_get ( reg, size ) ) )
						return EMULATE_FAULT;

				} else {

					if ( !read_rm ( size, value ) )
						return EMULATE_FAULT;

					if ( opcode <= 0x85 )
						alu ( 4, value, reg_get ( reg, size ), size );
					else if ( opcode <= 0x87 ) {

						if ( !write_rm ( size, reg_get ( reg, size ) ) )
							return EMULATE_FAULT;

						reg_set ( reg, size, value );

					} else
						reg_set ( reg, size, value );

				}

				finish ( );

				return EMULATE_DONE;

			}

			case 0xA0:
			case 0xA1:
			case 0xA2:
			case 0xA3: {

				uint32_t size = opcode & 1 ? operand : 1;
				uint64_t value = 0;

				offset = take ( address );
				memory = true;
				seal ( );

				if ( truncated )
					return EMULATE_UNSUPPORTED;

				if ( opcode <= 0xA1 ) {

					if ( !read_rm ( size, value ) )
						return EMULATE_FAULT;

					reg_set ( REG_AX, size, value );

				} else if ( !write_rm ( size, reg_get ( REG_AX, size ) ) )
					return EMULATE_FAULT;

				finish ( );

				return EMULATE_DONE;

			}

			case 0x6C:
			case 0x6D:
			case 0x6E:
			case 0x6F:
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
				return string ( );

			case 0xC6:
			case 0xC7: {

				uint32_t size = opcode & 1 ? operand : 1;
				uint32_t immediate_bytes = opcode & 1 ? immediate_size : 1;

				modrm ( );

				uint64_t immediate = sign_extend ( take ( immediate_bytes ), immediate_bytes );

				seal ( );

				if ( truncated || ( reg & 7 ) )
					return EMULATE_UNSUPPORTED;

				if ( !write_rm ( size, immediate ) )
					return EMULATE_FAULT;

				finish ( );

				return EMULATE_DONE;

			}

			case 0xE4:
			case 0xE5:
			case 0xE6:
			case 0xE7:
			case 0xEC:
			case 0xED:
			case 0xEE:
			case 0xEF: {

				uint32_t size = opcode & 1 ? immediate_size : 1;
				uint16_t port = opcode < 0xEC ? static_cast < uint16_t > ( take ( 1 ) ) : static_cast < uint16_t > ( cpu.gpr [ REG_DX ] );

				seal ( );

				if ( truncated )
					return EMULATE_UNSUPPORTED;

				if ( opcode & 2 )
					bus.out ( port, size, static_cast < uint32_t > ( cpu.gpr [ REG_AX ] & mask_of ( size ) ) );
				else
					reg_set ( REG_AX, size, bus.in ( port, size ) );

				finish ( );

				return EMULATE_DONE;

			}

			case 0xF6:
			case 0xFE:
				return unary ( 1 );

			case 0xF7:
			case 0xFF:
				return unary ( operand );

			default:
				return EMULATE_UNSUPPORTED;

		}

	}

	emulate_result emulate ( cpu_state& cpu, guest_bus& bus, const uint8_t* bytes, uint32_t count ) {

		executor work ( cpu, bus, bytes, count > 15 ? 15 : count );

		return work.run ( );

	}

}