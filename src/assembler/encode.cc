#include "assembler/internal.hh"

namespace assembler {

	enum form : uint8_t {

		FORM_PLAIN,
		FORM_DOUBLE,
		FORM_PREFIX,
		FORM_ALU,
		FORM_SHIFT,
		FORM_UNARY,
		FORM_STEP,
		FORM_JCC,
		FORM_LOOP,
		FORM_JMP,
		FORM_CALL,
		FORM_RET,
		FORM_RETF,
		FORM_INT,
		FORM_IN,
		FORM_OUT,
		FORM_MOV,
		FORM_TEST,
		FORM_XCHG,
		FORM_PUSH,
		FORM_POP,
		FORM_LOAD,

	};

	struct mnemonic {

		const char* name;
		form kind;
		uint8_t code;

	};

	static const mnemonic mnemonics [ ] = {

		{ "add", FORM_ALU, 0 }, { "or", FORM_ALU, 1 }, { "adc", FORM_ALU, 2 }, { "sbb", FORM_ALU, 3 },
		{ "and", FORM_ALU, 4 }, { "sub", FORM_ALU, 5 }, { "xor", FORM_ALU, 6 }, { "cmp", FORM_ALU, 7 },
		{ "rol", FORM_SHIFT, 0 }, { "ror", FORM_SHIFT, 1 }, { "rcl", FORM_SHIFT, 2 }, { "rcr", FORM_SHIFT, 3 },
		{ "shl", FORM_SHIFT, 4 }, { "sal", FORM_SHIFT, 4 }, { "shr", FORM_SHIFT, 5 }, { "sar", FORM_SHIFT, 7 },
		{ "not", FORM_UNARY, 2 }, { "neg", FORM_UNARY, 3 }, { "mul", FORM_UNARY, 4 }, { "imul", FORM_UNARY, 5 },
		{ "div", FORM_UNARY, 6 }, { "idiv", FORM_UNARY, 7 }, { "inc", FORM_STEP, 0 }, { "dec", FORM_STEP, 1 },
		{ "jo", FORM_JCC, 0x0 }, { "jno", FORM_JCC, 0x1 }, { "jb", FORM_JCC, 0x2 }, { "jc", FORM_JCC, 0x2 },
		{ "jnae", FORM_JCC, 0x2 }, { "jnb", FORM_JCC, 0x3 }, { "jae", FORM_JCC, 0x3 }, { "jnc", FORM_JCC, 0x3 },
		{ "je", FORM_JCC, 0x4 }, { "jz", FORM_JCC, 0x4 }, { "jne", FORM_JCC, 0x5 }, { "jnz", FORM_JCC, 0x5 },
		{ "jbe", FORM_JCC, 0x6 }, { "jna", FORM_JCC, 0x6 }, { "ja", FORM_JCC, 0x7 }, { "jnbe", FORM_JCC, 0x7 },
		{ "js", FORM_JCC, 0x8 }, { "jns", FORM_JCC, 0x9 }, { "jp", FORM_JCC, 0xA }, { "jpe", FORM_JCC, 0xA },
		{ "jnp", FORM_JCC, 0xB }, { "jpo", FORM_JCC, 0xB }, { "jl", FORM_JCC, 0xC }, { "jnge", FORM_JCC, 0xC },
		{ "jge", FORM_JCC, 0xD }, { "jnl", FORM_JCC, 0xD }, { "jle", FORM_JCC, 0xE }, { "jng", FORM_JCC, 0xE },
		{ "jg", FORM_JCC, 0xF }, { "jnle", FORM_JCC, 0xF },
		{ "loopne", FORM_LOOP, 0xE0 }, { "loopnz", FORM_LOOP, 0xE0 }, { "loope", FORM_LOOP, 0xE1 }, { "loopz", FORM_LOOP, 0xE1 },
		{ "loop", FORM_LOOP, 0xE2 }, { "jcxz", FORM_LOOP, 0xE3 },
		{ "jmp", FORM_JMP, 0 }, { "call", FORM_CALL, 0 }, { "ret", FORM_RET, 0 }, { "retn", FORM_RET, 0 }, { "retf", FORM_RETF, 0 },
		{ "int", FORM_INT, 0 }, { "in", FORM_IN, 0 }, { "out", FORM_OUT, 0 }, { "mov", FORM_MOV, 0 }, { "test", FORM_TEST, 0 },
		{ "xchg", FORM_XCHG, 0 }, { "push", FORM_PUSH, 0 }, { "pop", FORM_POP, 0 },
		{ "lea", FORM_LOAD, 0x8D }, { "les", FORM_LOAD, 0xC4 }, { "lds", FORM_LOAD, 0xC5 },
		{ "aam", FORM_DOUBLE, 0xD4 }, { "aad", FORM_DOUBLE, 0xD5 },
		{ "rep", FORM_PREFIX, 0xF3 }, { "repe", FORM_PREFIX, 0xF3 }, { "repz", FORM_PREFIX, 0xF3 }, { "repne", FORM_PREFIX, 0xF2 },
		{ "repnz", FORM_PREFIX, 0xF2 }, { "lock", FORM_PREFIX, 0xF0 }, { "es", FORM_PREFIX, 0x26 }, { "cs", FORM_PREFIX, 0x2E },
		{ "ss", FORM_PREFIX, 0x36 }, { "ds", FORM_PREFIX, 0x3E },
		{ "nop", FORM_PLAIN, 0x90 }, { "hlt", FORM_PLAIN, 0xF4 }, { "cli", FORM_PLAIN, 0xFA }, { "sti", FORM_PLAIN, 0xFB },
		{ "cld", FORM_PLAIN, 0xFC }, { "std", FORM_PLAIN, 0xFD }, { "clc", FORM_PLAIN, 0xF8 }, { "stc", FORM_PLAIN, 0xF9 },
		{ "cmc", FORM_PLAIN, 0xF5 }, { "pushf", FORM_PLAIN, 0x9C }, { "popf", FORM_PLAIN, 0x9D }, { "sahf", FORM_PLAIN, 0x9E },
		{ "lahf", FORM_PLAIN, 0x9F }, { "cbw", FORM_PLAIN, 0x98 }, { "cwd", FORM_PLAIN, 0x99 }, { "xlat", FORM_PLAIN, 0xD7 },
		{ "xlatb", FORM_PLAIN, 0xD7 }, { "iret", FORM_PLAIN, 0xCF }, { "into", FORM_PLAIN, 0xCE }, { "int3", FORM_PLAIN, 0xCC },
		{ "aaa", FORM_PLAIN, 0x37 }, { "aas", FORM_PLAIN, 0x3F }, { "daa", FORM_PLAIN, 0x27 }, { "das", FORM_PLAIN, 0x2F },
		{ "wait", FORM_PLAIN, 0x9B }, { "movsb", FORM_PLAIN, 0xA4 }, { "movsw", FORM_PLAIN, 0xA5 }, { "cmpsb", FORM_PLAIN, 0xA6 },
		{ "cmpsw", FORM_PLAIN, 0xA7 }, { "stosb", FORM_PLAIN, 0xAA }, { "stosw", FORM_PLAIN, 0xAB }, { "lodsb", FORM_PLAIN, 0xAC },
		{ "lodsw", FORM_PLAIN, 0xAD }, { "scasb", FORM_PLAIN, 0xAE }, { "scasw", FORM_PLAIN, 0xAF },

	};

	static const char* const byte_registers [ 8 ] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };
	static const char* const word_registers [ 8 ] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
	static const char* const segment_registers [ 4 ] = { "es", "cs", "ss", "ds" };

	static bool is_mark ( const token& item, char mark ) {

		return item.kind == TOKEN_MARK && item.mark == mark;

	}

	static bool fits_byte ( int64_t value ) {

		return value >= -128 && value <= 127;

	}

	static void prefix ( emitter& out, const operand& target ) {

		if ( target.kind == OPERAND_MEMORY && target.override != 0xFF )
			out.byte ( 0x26 | target.override << 3 );

	}

	bool program::parse_operand ( int32_t first, int32_t count, operand& result ) {

		int32_t at = first;
		int32_t end = first + count;

		result = operand ( );

		while ( at < end && tokens [ at ].kind == TOKEN_NAME ) {

			const token& item = tokens [ at ];

			if ( same ( item, "byte" ) )
				result.size = 1;
			else if ( same ( item, "word" ) )
				result.size = 2;
			else if ( same ( item, "far" ) )
				result.far_hint = true;
			else if ( same ( item, "near" ) )
				result.near_hint = true;
			else if ( same ( item, "short" ) )
				result.short_hint = true;
			else if ( !same ( item, "ptr" ) )
				break;

			at++;

		}

		if ( at >= end )
			return fail ( "operand expected" );

		for ( uint8_t s = 0; s < 4; s++ ) {

			if ( at + 2 < end && same ( tokens [ at ], segment_registers [ s ] ) && is_mark ( tokens [ at + 1 ], ':' ) && is_mark ( tokens [ at + 2 ], '[' ) ) {

				result.override = s;
				at += 2;

			}

		}

		if ( is_mark ( tokens [ at ], '[' ) ) {

			if ( !is_mark ( tokens [ end - 1 ], ']' ) )
				return fail ( "missing ]" );

			int32_t inner = at + 1;
			int32_t close = end - 1;

			for ( uint8_t s = 0; s < 4; s++ ) {

				if ( close - inner >= 2 && same ( tokens [ inner ], segment_registers [ s ] ) && is_mark ( tokens [ inner + 1 ], ':' ) ) {

					result.override = s;
					inner += 2;

				}

			}

			bool bx = false;
			bool bp = false;
			bool si = false;
			bool di = false;
			bool any = false;
			int64_t displacement = 0;
			int32_t start = inner;
			int32_t sign = 1;
			int32_t depth = 0;

			for ( int32_t i = inner; i <= close; i++ ) {

				const token& item = tokens [ i ];
				bool last = i == close;

				if ( !last && is_mark ( item, '(' ) )
					depth++;
				else if ( !last && is_mark ( item, ')' ) )
					depth--;

				bool cut = last || ( depth == 0 && ( is_mark ( item, '+' ) || is_mark ( item, '-' ) ) );

				if ( !cut )
					continue;

				if ( i == start ) {

					if ( last )
						break;

					if ( is_mark ( item, '-' ) )
						sign = -sign;

					start = i + 1;

					continue;

				}

				if ( i - start == 1 && tokens [ start ].kind == TOKEN_NAME && ( same ( tokens [ start ], "bx" ) || same ( tokens [ start ], "bp" ) || same ( tokens [ start ], "si" ) || same ( tokens [ start ], "di" ) ) ) {

					const token& name = tokens [ start ];
					bool* slot = same ( name, "bx" ) ? &bx : ( same ( name, "bp" ) ? &bp : ( same ( name, "si" ) ? &si : &di ) );

					if ( sign < 0 || *slot )
						return fail ( "bad address", name );

					*slot = true;

				} else {

					for ( int32_t r = 0; r < 8; r++ ) {

						if ( i - start == 1 && ( same ( tokens [ start ], byte_registers [ r ] ) || same ( tokens [ start ], word_registers [ r ] ) ) )
							return fail ( "only bx, bp, si and di can address memory", tokens [ start ] );

					}

					int64_t value = 0;

					if ( !evaluate ( start, i - start, value, result.known ) )
						return false;

					displacement += sign * value;
					any = true;

				}

				sign = is_mark ( item, '-' ) ? -1 : 1;
				start = i + 1;

			}

			if ( ( bx && bp ) || ( si && di ) )
				return fail ( "bad register pair in address" );

			result.kind = OPERAND_MEMORY;
			result.value = static_cast < int32_t > ( displacement );
			result.direct = !bx && !bp && !si && !di;

			if ( bx && si )
				result.rm = 0;
			else if ( bx && di )
				result.rm = 1;
			else if ( bp && si )
				result.rm = 2;
			else if ( bp && di )
				result.rm = 3;
			else if ( si )
				result.rm = 4;
			else if ( di )
				result.rm = 5;
			else if ( bp )
				result.rm = 6;
			else if ( bx )
				result.rm = 7;
			else
				result.rm = 6;

			if ( !any && !result.direct )
				result.value = 0;

			return true;

		}

		if ( at + 1 == end && tokens [ at ].kind == TOKEN_NAME ) {

			for ( uint8_t r = 0; r < 8; r++ ) {

				if ( same ( tokens [ at ], byte_registers [ r ] ) ) {

					result.kind = OPERAND_REGISTER;
					result.number = r;
					result.size = 1;

					return true;

				}

				if ( same ( tokens [ at ], word_registers [ r ] ) ) {

					result.kind = OPERAND_REGISTER;
					result.number = r;
					result.size = 2;

					return true;

				}

			}

			for ( uint8_t s = 0; s < 4; s++ ) {

				if ( same ( tokens [ at ], segment_registers [ s ] ) ) {

					result.kind = OPERAND_SEGMENT;
					result.number = s;
					result.size = 2;

					return true;

				}

			}

		}

		int32_t depth = 0;

		for ( int32_t i = at; i < end; i++ ) {

			if ( is_mark ( tokens [ i ], '(' ) )
				depth++;
			else if ( is_mark ( tokens [ i ], ')' ) )
				depth--;
			else if ( depth == 0 && is_mark ( tokens [ i ], ':' ) ) {

				int64_t segment_value = 0;
				int64_t offset_value = 0;

				if ( !evaluate ( at, i - at, segment_value, result.known ) || !evaluate ( i + 1, end - i - 1, offset_value, result.known ) )
					return false;

				result.kind = OPERAND_FAR;
				result.segment_value = static_cast < int32_t > ( segment_value );
				result.value = static_cast < int32_t > ( offset_value );

				return true;

			}

		}

		int64_t value = 0;

		if ( !evaluate ( at, end - at, value, result.known ) )
			return false;

		result.kind = OPERAND_IMMEDIATE;
		result.value = static_cast < int32_t > ( value );

		return true;

	}

	static void modrm ( program& owner, statement& item, emitter& out, uint32_t field, const operand& target ) {

		if ( target.kind == OPERAND_REGISTER ) {

			out.byte ( 0xC0 | field << 3 | target.number );

			return;

		}

		if ( target.direct ) {

			out.byte ( field << 3 | 6 );
			out.word ( static_cast < uint32_t > ( target.value ) );

			return;

		}

		bool none = target.value == 0 && target.rm != 6;
		bool fits = !target.known || fits_byte ( target.value );

		if ( owner.small ( item, fits ) ) {

			if ( none && target.known ) {

				out.byte ( field << 3 | target.rm );

				return;

			}

			out.byte ( 0x40 | field << 3 | target.rm );
			out.byte ( static_cast < uint32_t > ( target.value ) );

			return;

		}

		out.byte ( 0x80 | field << 3 | target.rm );
		out.word ( static_cast < uint32_t > ( target.value ) );

	}

	static bool rm_like ( const operand& target ) {

		return target.kind == OPERAND_REGISTER || target.kind == OPERAND_MEMORY;

	}

	bool program::encode ( statement& item, int32_t first, int32_t count, emitter& out ) {

		const token& head = tokens [ first ];
		const mnemonic* entry = nullptr;

		for ( const mnemonic& candidate : mnemonics ) {

			if ( same ( head, candidate.name ) ) {

				entry = &candidate;

				break;

			}

		}

		if ( !entry )
			return fail ( "unknown instruction", head );

		if ( entry->kind == FORM_PREFIX ) {

			out.byte ( entry->code );

			return count > 1 ? encode ( item, first + 1, count - 1, out ) : true;

		}

		operand list [ 3 ];
		int32_t total = 0;
		int32_t starts [ 3 ];
		int32_t counts [ 3 ];

		if ( count > 1 ) {

			if ( !split ( first + 1, count - 1, starts, counts, total, 3 ) )
				return false;

			for ( int32_t i = 0; i < total; i++ ) {

				if ( !parse_operand ( starts [ i ], counts [ i ], list [ i ] ) )
					return false;

			}

		}

		operand& a = list [ 0 ];
		operand& b = list [ 1 ];
		uint32_t here = item.address + static_cast < uint32_t > ( out.size );

		auto value_check = [ & ] ( const operand& value, int32_t size ) -> bool {

			if ( !final )
				return true;

			if ( !value.known )
				return fail ( "value isn't known", head );

			if ( size == 1 && ( value.value < -128 || value.value > 255 ) )
				return fail ( "value doesn't fit in a byte", head );

			if ( size == 2 && ( value.value < -32768 || value.value > 65535 ) )
				return fail ( "value doesn't fit in a word", head );

			return true;

		};

		auto size_of = [ & ] ( const operand& x, const operand& y ) -> int32_t {

			if ( x.size && y.size && x.size != y.size && x.kind != OPERAND_IMMEDIATE && y.kind != OPERAND_IMMEDIATE )
				return -1;

			return x.size ? x.size : y.size;

		};

		auto operands = [ & ] ( int32_t wanted ) -> bool {

			if ( total != wanted )
				return fail ( wanted == 0 ? "takes no operands" : ( wanted == 1 ? "takes one operand" : "takes two operands" ), head );

			return true;

		};

		switch ( entry->kind ) {

			case FORM_PLAIN:

				if ( !operands ( 0 ) )
					return false;

				out.byte ( entry->code );

				return true;

			case FORM_DOUBLE:

				out.byte ( entry->code );
				out.byte ( total ? static_cast < uint32_t > ( a.value ) : 10 );

				return true;

			case FORM_ALU: {

				if ( !operands ( 2 ) )
					return false;

				uint32_t group = entry->code;
				int32_t size = size_of ( a, b );

				if ( size < 0 )
					return fail ( "operand sizes don't match", head );

				if ( b.kind == OPERAND_IMMEDIATE ) {

					if ( !rm_like ( a ) )
						return fail ( "bad destination", head );

					if ( !size )
						return fail ( "say byte or word", head );

					prefix ( out, a );

					if ( a.kind == OPERAND_REGISTER && a.number == 0 ) {

						out.byte ( group * 8 + ( size == 1 ? 4 : 5 ) );

						if ( size == 1 )
							out.byte ( static_cast < uint32_t > ( b.value ) );
						else
							out.word ( static_cast < uint32_t > ( b.value ) );

						return value_check ( b, size );

					}

					if ( size == 1 ) {

						out.byte ( 0x80 );
						modrm ( *this, item, out, group, a );
						out.byte ( static_cast < uint32_t > ( b.value ) );

						return value_check ( b, 1 );

					}

					if ( small ( item, !b.known || fits_byte ( b.value ) ) ) {

						out.byte ( 0x83 );
						modrm ( *this, item, out, group, a );
						out.byte ( static_cast < uint32_t > ( b.value ) );

						return !final || fits_byte ( b.value ) || fail ( "value doesn't fit", head );

					}

					out.byte ( 0x81 );
					modrm ( *this, item, out, group, a );
					out.word ( static_cast < uint32_t > ( b.value ) );

					return value_check ( b, 2 );

				}

				if ( a.kind == OPERAND_REGISTER && rm_like ( b ) ) {

					prefix ( out, b );
					out.byte ( group * 8 + ( size == 1 ? 2 : 3 ) );
					modrm ( *this, item, out, a.number, b );

					return true;

				}

				if ( a.kind == OPERAND_MEMORY && b.kind == OPERAND_REGISTER ) {

					prefix ( out, a );
					out.byte ( group * 8 + ( size == 1 ? 0 : 1 ) );
					modrm ( *this, item, out, b.number, a );

					return true;

				}

				return fail ( "bad operands", head );

			}

			case FORM_SHIFT: {

				if ( !operands ( 2 ) || !rm_like ( a ) )
					return fail ( "bad operands", head );

				if ( !a.size )
					return fail ( "say byte or word", head );

				uint32_t base_code = a.size == 1 ? 0xD0 : 0xD1;

				if ( b.kind == OPERAND_REGISTER && b.size == 1 && b.number == 1 ) {

					prefix ( out, a );
					out.byte ( base_code + 2 );
					modrm ( *this, item, out, entry->code, a );

					return true;

				}

				if ( b.kind != OPERAND_IMMEDIATE || ( final && ( b.value < 1 || b.value > 8 ) ) )
					return fail ( "shift by 1 to 8 or by cl", head );

				int32_t repeat = base::clamp ( b.value, 1, 8 );

				for ( int32_t i = 0; i < repeat; i++ ) {

					prefix ( out, a );
					out.byte ( base_code );
					modrm ( *this, item, out, entry->code, a );

				}

				return true;

			}

			case FORM_UNARY:
			case FORM_STEP: {

				if ( !operands ( 1 ) || !rm_like ( a ) )
					return fail ( "bad operand", head );

				if ( !a.size )
					return fail ( "say byte or word", head );

				if ( entry->kind == FORM_STEP && a.kind == OPERAND_REGISTER && a.size == 2 ) {

					out.byte ( ( entry->code ? 0x48 : 0x40 ) + a.number );

					return true;

				}

				prefix ( out, a );

				if ( entry->kind == FORM_STEP ) {

					out.byte ( a.size == 1 ? 0xFE : 0xFF );
					modrm ( *this, item, out, entry->code, a );

				} else {

					out.byte ( a.size == 1 ? 0xF6 : 0xF7 );
					modrm ( *this, item, out, entry->code, a );

				}

				return true;

			}

			case FORM_JCC:
			case FORM_LOOP: {

				if ( !operands ( 1 ) || a.kind != OPERAND_IMMEDIATE )
					return fail ( "jump needs a target", head );

				int64_t short_distance = static_cast < int64_t > ( a.value ) - ( here + 2 );
				bool fits = !a.known || fits_byte ( short_distance );

				if ( entry->kind == FORM_LOOP || a.short_hint ) {

					out.byte ( entry->kind == FORM_LOOP ? entry->code : 0x70 + entry->code );
					out.byte ( static_cast < uint32_t > ( short_distance ) );

					return !final || fits_byte ( short_distance ) || fail ( "short jump is out of range", head );

				}

				if ( !a.near_hint && small ( item, fits ) ) {

					out.byte ( 0x70 + entry->code );
					out.byte ( static_cast < uint32_t > ( short_distance ) );

					return true;

				}

				item.wide = true;
				out.byte ( 0x70 + ( entry->code ^ 1 ) );
				out.byte ( 3 );
				out.byte ( 0xE9 );
				out.word ( static_cast < uint32_t > ( a.value - ( here + 5 ) ) );

				return true;

			}

			case FORM_JMP:
			case FORM_CALL: {

				if ( !operands ( 1 ) )
					return false;

				bool jump = entry->kind == FORM_JMP;

				if ( a.kind == OPERAND_FAR ) {

					out.byte ( jump ? 0xEA : 0x9A );
					out.word ( static_cast < uint32_t > ( a.value ) );
					out.word ( static_cast < uint32_t > ( a.segment_value ) );

					return true;

				}

				if ( rm_like ( a ) ) {

					prefix ( out, a );
					out.byte ( 0xFF );
					modrm ( *this, item, out, ( jump ? 4 : 2 ) + ( a.far_hint ? 1 : 0 ), a );

					return true;

				}

				if ( a.kind != OPERAND_IMMEDIATE )
					return fail ( "bad target", head );

				if ( jump ) {

					int64_t short_distance = static_cast < int64_t > ( a.value ) - ( here + 2 );

					if ( a.short_hint || ( !a.near_hint && small ( item, !a.known || fits_byte ( short_distance ) ) ) ) {

						out.byte ( 0xEB );
						out.byte ( static_cast < uint32_t > ( short_distance ) );

						return !final || fits_byte ( short_distance ) || fail ( "short jump is out of range", head );

					}

					item.wide = true;

				}

				out.byte ( jump ? 0xE9 : 0xE8 );
				out.word ( static_cast < uint32_t > ( a.value - ( here + 3 ) ) );

				return true;

			}

			case FORM_RET:
			case FORM_RETF:

				if ( total == 0 ) {

					out.byte ( entry->kind == FORM_RET ? 0xC3 : 0xCB );

					return true;

				}

				out.byte ( entry->kind == FORM_RET ? 0xC2 : 0xCA );
				out.word ( static_cast < uint32_t > ( a.value ) );

				return value_check ( a, 2 );

			case FORM_INT:

				if ( !operands ( 1 ) || a.kind != OPERAND_IMMEDIATE )
					return fail ( "int needs a number", head );

				out.byte ( 0xCD );
				out.byte ( static_cast < uint32_t > ( a.value ) );

				return value_check ( a, 1 );

			case FORM_IN:
			case FORM_OUT: {

				if ( !operands ( 2 ) )
					return false;

				bool input = entry->kind == FORM_IN;
				operand& data = input ? a : b;
				operand& port = input ? b : a;

				if ( data.kind != OPERAND_REGISTER || data.number != 0 )
					return fail ( "in and out use al or ax", head );

				if ( port.kind == OPERAND_REGISTER && port.size == 2 && port.number == 2 ) {

					out.byte ( ( input ? 0xEC : 0xEE ) + ( data.size == 2 ? 1 : 0 ) );

					return true;

				}

				if ( port.kind != OPERAND_IMMEDIATE )
					return fail ( "port must be a number or dx", head );

				out.byte ( ( input ? 0xE4 : 0xE6 ) + ( data.size == 2 ? 1 : 0 ) );
				out.byte ( static_cast < uint32_t > ( port.value ) );

				return !final || ( port.value >= 0 && port.value <= 255 ) || fail ( "port numbers above FF need dx", head );

			}

			case FORM_MOV: {

				if ( !operands ( 2 ) )
					return false;

				if ( a.kind == OPERAND_SEGMENT ) {

					if ( a.number == 1 || !rm_like ( b ) || b.size == 1 )
						return fail ( "bad segment load", head );

					prefix ( out, b );
					out.byte ( 0x8E );
					modrm ( *this, item, out, a.number, b );

					return true;

				}

				if ( b.kind == OPERAND_SEGMENT ) {

					if ( !rm_like ( a ) || a.size == 1 )
						return fail ( "bad segment store", head );

					prefix ( out, a );
					out.byte ( 0x8C );
					modrm ( *this, item, out, b.number, a );

					return true;

				}

				int32_t size = size_of ( a, b );

				if ( size < 0 )
					return fail ( "operand sizes don't match", head );

				if ( b.kind == OPERAND_IMMEDIATE ) {

					if ( !size )
						return fail ( "say byte or word", head );

					if ( a.kind == OPERAND_REGISTER ) {

						out.byte ( ( size == 1 ? 0xB0 : 0xB8 ) + a.number );

						if ( size == 1 )
							out.byte ( static_cast < uint32_t > ( b.value ) );
						else
							out.word ( static_cast < uint32_t > ( b.value ) );

						return value_check ( b, size );

					}

					if ( a.kind != OPERAND_MEMORY )
						return fail ( "bad destination", head );

					prefix ( out, a );
					out.byte ( size == 1 ? 0xC6 : 0xC7 );
					modrm ( *this, item, out, 0, a );

					if ( size == 1 )
						out.byte ( static_cast < uint32_t > ( b.value ) );
					else
						out.word ( static_cast < uint32_t > ( b.value ) );

					return value_check ( b, size );

				}

				if ( a.kind == OPERAND_REGISTER && b.kind == OPERAND_MEMORY && a.number == 0 && b.direct ) {

					prefix ( out, b );
					out.byte ( size == 1 ? 0xA0 : 0xA1 );
					out.word ( static_cast < uint32_t > ( b.value ) );

					return true;

				}

				if ( a.kind == OPERAND_MEMORY && b.kind == OPERAND_REGISTER && b.number == 0 && a.direct ) {

					prefix ( out, a );
					out.byte ( size == 1 ? 0xA2 : 0xA3 );
					out.word ( static_cast < uint32_t > ( a.value ) );

					return true;

				}

				if ( a.kind == OPERAND_REGISTER && rm_like ( b ) ) {

					prefix ( out, b );
					out.byte ( size == 1 ? 0x8A : 0x8B );
					modrm ( *this, item, out, a.number, b );

					return true;

				}

				if ( a.kind == OPERAND_MEMORY && b.kind == OPERAND_REGISTER ) {

					prefix ( out, a );
					out.byte ( size == 1 ? 0x88 : 0x89 );
					modrm ( *this, item, out, b.number, a );

					return true;

				}

				return fail ( "bad operands", head );

			}

			case FORM_TEST: {

				if ( !operands ( 2 ) )
					return false;

				int32_t size = size_of ( a, b );

				if ( size <= 0 )
					return fail ( size < 0 ? "operand sizes don't match" : "say byte or word", head );

				if ( b.kind == OPERAND_IMMEDIATE ) {

					if ( a.kind == OPERAND_REGISTER && a.number == 0 ) {

						out.byte ( size == 1 ? 0xA8 : 0xA9 );

					} else {

						if ( !rm_like ( a ) )
							return fail ( "bad operands", head );

						prefix ( out, a );
						out.byte ( size == 1 ? 0xF6 : 0xF7 );
						modrm ( *this, item, out, 0, a );

					}

					if ( size == 1 )
						out.byte ( static_cast < uint32_t > ( b.value ) );
					else
						out.word ( static_cast < uint32_t > ( b.value ) );

					return value_check ( b, size );

				}

				operand& reg = b.kind == OPERAND_REGISTER ? b : a;
				operand& other = b.kind == OPERAND_REGISTER ? a : b;

				if ( reg.kind != OPERAND_REGISTER || !rm_like ( other ) )
					return fail ( "bad operands", head );

				prefix ( out, other );
				out.byte ( size == 1 ? 0x84 : 0x85 );
				modrm ( *this, item, out, reg.number, other );

				return true;

			}

			case FORM_XCHG: {

				if ( !operands ( 2 ) )
					return false;

				int32_t size = size_of ( a, b );

				if ( size <= 0 )
					return fail ( "bad operands", head );

				if ( size == 2 && a.kind == OPERAND_REGISTER && b.kind == OPERAND_REGISTER && ( a.number == 0 || b.number == 0 ) ) {

					out.byte ( 0x90 + ( a.number == 0 ? b.number : a.number ) );

					return true;

				}

				operand& reg = b.kind == OPERAND_REGISTER ? b : a;
				operand& other = b.kind == OPERAND_REGISTER ? a : b;

				if ( reg.kind != OPERAND_REGISTER || !rm_like ( other ) )
					return fail ( "bad operands", head );

				prefix ( out, other );
				out.byte ( size == 1 ? 0x86 : 0x87 );
				modrm ( *this, item, out, reg.number, other );

				return true;

			}

			case FORM_PUSH:
			case FORM_POP: {

				if ( !operands ( 1 ) )
					return false;

				bool push = entry->kind == FORM_PUSH;

				if ( a.kind == OPERAND_SEGMENT ) {

					if ( !push && a.number == 1 )
						return fail ( "can't pop cs", head );

					out.byte ( ( push ? 0x06 : 0x07 ) + a.number * 8 );

					return true;

				}

				if ( a.kind == OPERAND_REGISTER && a.size == 2 ) {

					out.byte ( ( push ? 0x50 : 0x58 ) + a.number );

					return true;

				}

				if ( a.kind == OPERAND_MEMORY && a.size != 1 ) {

					prefix ( out, a );
					out.byte ( push ? 0xFF : 0x8F );
					modrm ( *this, item, out, push ? 6 : 0, a );

					return true;

				}

				return fail ( "push and pop take a word register, segment or memory", head );

			}

			case FORM_LOAD:

				if ( !operands ( 2 ) || a.kind != OPERAND_REGISTER || a.size != 2 || b.kind != OPERAND_MEMORY )
					return fail ( "needs a word register and memory", head );

				prefix ( out, b );
				out.byte ( entry->code );
				modrm ( *this, item, out, a.number, b );

				return true;

			default:
				return fail ( "unknown instruction", head );

		}

	}

}