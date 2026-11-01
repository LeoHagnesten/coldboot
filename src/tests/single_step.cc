#ifdef COLDBOOT_DEBUG

#include "base/files.hh"
#include "base/inflate.hh"
#include "cpu/cpu.hh"
#include "tests/harness.hh"
#include <windows.h>

namespace tests {

	static bool exact_flags = false;
	static bool show_timing = false;

	struct json {

		const char* at = nullptr;
		const char* end = nullptr;
		bool failed = false;

		void space ( ) {

			while ( at < end && ( *at == ' ' || *at == '\n' || *at == '\r' || *at == '\t' ) )
				at++;

		}

		bool consume ( char value ) {

			space ( );

			if ( at < end && *at == value ) {

				at++;

				return true;

			}

			return false;

		}

		void need ( char value ) {

			if ( !consume ( value ) )
				failed = true;

		}

		int64_t number ( ) {

			space ( );

			bool negative = false;

			if ( at < end && *at == '-' ) {

				negative = true;
				at++;

			}

			int64_t value = 0;

			while ( at < end && *at >= '0' && *at <= '9' )
				value = value * 10 + ( *at++ - '0' );

			while ( at < end && ( *at == '.' || *at == 'e' || *at == 'E' || *at == '+' || *at == '-' || ( *at >= '0' && *at <= '9' ) ) )
				at++;

			return negative ? -value : value;

		}

		void string ( char* out, int32_t room ) {

			space ( );

			int32_t length = 0;

			if ( at >= end || *at != '"' ) {

				failed = true;

				if ( room )
					out [ 0 ] = 0;

				return;

			}

			at++;

			while ( at < end && *at != '"' ) {

				char c = *at++;

				if ( c == '\\' && at < end )
					c = *at++;

				if ( length < room - 1 )
					out [ length++ ] = c;

			}

			at++;

			if ( room )
				out [ length ] = 0;

		}

		void skip ( ) {

			space ( );

			if ( at >= end ) {

				failed = true;

				return;

			}

			if ( *at == '"' ) {

				char scratch [ 4 ];

				string ( scratch, 1 );

				return;

			}

			if ( *at == '{' || *at == '[' ) {

				char close = *at == '{' ? '}' : ']';

				at++;

				if ( consume ( close ) )
					return;

				while ( !failed ) {

					if ( close == '}' ) {

						char key [ 4 ];

						string ( key, 1 );
						need ( ':' );

					}

					skip ( );

					if ( consume ( ',' ) )
						continue;

					need ( close );

					return;

				}

				return;

			}

			while ( at < end && *at != ',' && *at != '}' && *at != ']' )
				at++;

		}

	};

	static const char* const register_names [ 14 ] = { "ax", "bx", "cx", "dx", "cs", "ss", "ds", "es", "sp", "bp", "si", "di", "ip", "flags" };

	static uint16_t& register_slot ( machine::registers& r, int32_t index ) {

		static const uint8_t generals [ 4 ] = { machine::REG_AX, machine::REG_BX, machine::REG_CX, machine::REG_DX };
		static const uint8_t segments [ 4 ] = { machine::SEG_CS, machine::SEG_SS, machine::SEG_DS, machine::SEG_ES };
		static const uint8_t pointers [ 4 ] = { machine::REG_SP, machine::REG_BP, machine::REG_SI, machine::REG_DI };

		if ( index < 4 )
			return r.general [ generals [ index ] ];

		if ( index < 8 )
			return r.segments [ segments [ index - 4 ] ];

		if ( index < 12 )
			return r.general [ pointers [ index - 8 ] ];

		return index == 12 ? r.ip : r.flags;

	}

	struct test_state {

		uint16_t regs [ 14 ] = { };
		bool has [ 14 ] = { };
		uint32_t queued = 0;
		base::list < uint32_t > addresses;
		base::list < uint8_t > values;

		void clear ( ) {

			queued = 0;

			for ( int32_t i = 0; i < 14; i++ )
				has [ i ] = false;

			addresses.clear ( );
			values.clear ( );

		}

	};

	struct test_case {

		char name [ 160 ] = { };
		int64_t index = 0;
		int64_t clocks = 0;
		test_state initial;
		test_state final;

	};

	static void parse_state ( json& in, test_state& state ) {

		state.clear ( );
		in.need ( '{' );

		if ( in.consume ( '}' ) )
			return;

		while ( !in.failed ) {

			char key [ 16 ];

			in.string ( key, 16 );
			in.need ( ':' );

			if ( base::same_text ( key, "regs" ) ) {

				in.need ( '{' );

				if ( !in.consume ( '}' ) ) {

					while ( !in.failed ) {

						char name [ 16 ];

						in.string ( name, 16 );
						in.need ( ':' );

						uint16_t value = static_cast < uint16_t > ( in.number ( ) );

						for ( int32_t i = 0; i < 14; i++ ) {

							if ( base::same_text ( name, register_names [ i ] ) ) {

								state.regs [ i ] = value;
								state.has [ i ] = true;

							}

						}

						if ( in.consume ( ',' ) )
							continue;

						in.need ( '}' );

						break;

					}

				}

			} else if ( base::same_text ( key, "ram" ) ) {

				in.need ( '[' );

				if ( !in.consume ( ']' ) ) {

					while ( !in.failed ) {

						in.need ( '[' );

						uint32_t address = static_cast < uint32_t > ( in.number ( ) );

						in.need ( ',' );

						uint8_t value = static_cast < uint8_t > ( in.number ( ) );

						in.need ( ']' );
						state.addresses.push ( address );
						state.values.push ( value );

						if ( in.consume ( ',' ) )
							continue;

						in.need ( ']' );

						break;

					}

				}

			} else if ( base::same_text ( key, "queue" ) ) {

				in.need ( '[' );

				if ( !in.consume ( ']' ) ) {

					while ( !in.failed ) {

						in.number ( );
						state.queued++;

						if ( in.consume ( ',' ) )
							continue;

						in.need ( ']' );

						break;

					}

				}

			} else
				in.skip ( );

			if ( in.consume ( ',' ) )
				continue;

			in.need ( '}' );

			return;

		}

	}

	static bool parse_test ( json& in, test_case& test ) {

		in.need ( '{' );

		while ( !in.failed ) {

			char key [ 16 ];

			in.string ( key, 16 );
			in.need ( ':' );

			if ( base::same_text ( key, "name" ) )
				in.string ( test.name, 160 );
			else if ( base::same_text ( key, "idx" ) )
				test.index = in.number ( );
			else if ( base::same_text ( key, "initial" ) )
				parse_state ( in, test.initial );
			else if ( base::same_text ( key, "final" ) )
				parse_state ( in, test.final );
			else if ( base::same_text ( key, "cycles" ) ) {

				test.clocks = 0;
				in.need ( '[' );

				if ( !in.consume ( ']' ) ) {

					while ( !in.failed ) {

						in.skip ( );
						test.clocks++;

						if ( in.consume ( ',' ) )
							continue;

						in.need ( ']' );

						break;

					}

				}

			} else
				in.skip ( );

			if ( in.consume ( ',' ) )
				continue;

			in.need ( '}' );

			break;

		}

		return !in.failed;

	}

	struct opcode_info {

		uint16_t mask [ 9 ];
		bool skip [ 9 ];

	};

	static opcode_info metadata [ 256 ];

	static void parse_info ( json& in, int32_t opcode, int32_t slot ) {

		in.need ( '{' );

		if ( in.consume ( '}' ) )
			return;

		while ( !in.failed ) {

			char key [ 16 ];

			in.string ( key, 16 );
			in.need ( ':' );

			if ( base::same_text ( key, "flags-mask" ) )
				metadata [ opcode ].mask [ slot ] = static_cast < uint16_t > ( in.number ( ) );
			else if ( base::same_text ( key, "status" ) ) {

				char status [ 32 ];

				in.string ( status, 32 );
				metadata [ opcode ].skip [ slot ] = base::same_text ( status, "undefined" ) || base::same_text ( status, "prefix" );

			} else if ( base::same_text ( key, "reg" ) ) {

				in.need ( '{' );

				while ( !in.failed ) {

					char reg_key [ 8 ];

					in.string ( reg_key, 8 );
					in.need ( ':' );
					parse_info ( in, opcode, reg_key [ 0 ] - '0' );

					if ( in.consume ( ',' ) )
						continue;

					in.need ( '}' );

					break;

				}

			} else
				in.skip ( );

			if ( in.consume ( ',' ) )
				continue;

			in.need ( '}' );

			return;

		}

	}

	static int32_t hex_digit ( wchar_t c ) {

		if ( c >= L'0' && c <= L'9' )
			return c - L'0';

		if ( c >= L'A' && c <= L'F' )
			return c - L'A' + 10;

		if ( c >= L'a' && c <= L'f' )
			return c - L'a' + 10;

		return -1;

	}

	static bool load_metadata ( const wchar_t* folder ) {

		for ( int32_t i = 0; i < 256; i++ ) {

			for ( int32_t k = 0; k < 9; k++ ) {

				metadata [ i ].mask [ k ] = 0xFFFF;
				metadata [ i ].skip [ k ] = false;

			}

		}

		base::writer path;
		base::list < char > text;

		path.add ( folder ).add ( L"\\metadata.json" );

		if ( !files::read ( path.text ( ), text ) )
			return false;

		json in;

		in.at = text.data ( );
		in.end = text.data ( ) + text.count ( ) - 1;
		in.need ( '{' );

		while ( !in.failed ) {

			char key [ 32 ];

			in.string ( key, 32 );
			in.need ( ':' );

			if ( base::same_text ( key, "opcodes" ) ) {

				in.need ( '{' );

				while ( !in.failed ) {

					char opcode_key [ 8 ];

					in.string ( opcode_key, 8 );
					in.need ( ':' );

					int32_t opcode = hex_digit ( opcode_key [ 0 ] ) * 16 + hex_digit ( opcode_key [ 1 ] );

					if ( opcode >= 0 && opcode < 256 )
						parse_info ( in, opcode, 8 );
					else
						in.skip ( );

					if ( in.consume ( ',' ) )
						continue;

					in.need ( '}' );

					break;

				}

			} else
				in.skip ( );

			if ( in.consume ( ',' ) )
				continue;

			break;

		}

		return !in.failed;

	}

	struct file_job {

		wchar_t path [ 600 ] = { };
		char label [ 32 ] = { };
		int32_t opcode = -1;
		int32_t slot = 8;
		int32_t total = 0;
		int32_t passed = 0;
		int64_t our_clocks = 0;
		int64_t real_clocks = 0;
		int32_t close = 0;
		bool skipped = false;
		bool broken = false;
		char problems [ 3 ] [ 400 ] = { };
		int32_t problem_count = 0;

	};

	static base::list < file_job > jobs;
	static volatile long next_job = -1;

	static bool run_case ( machine::cpu& computer, test_case& test, uint16_t flags_mask, char* problem, int32_t room ) {

		machine::registers& r = computer.state;
		machine::bus& memory = computer.system;
		base::list < uint32_t > writes;

		for ( int32_t i = 0; i < 14; i++ )
			register_slot ( r, i ) = test.initial.regs [ i ];

		for ( int32_t i = 0; i < test.initial.addresses.count ( ); i++ )
			memory.write8 ( test.initial.addresses [ static_cast < size_t > ( i ) ], test.initial.values [ static_cast < size_t > ( i ) ] );

		computer.halted = false;
		computer.queue = test.initial.queued;
		memory.write_log = &writes;
		computer.step ( );
		memory.write_log = nullptr;

		bool ok = true;

		problem [ 0 ] = 0;

		for ( int32_t i = 0; i < 14 && ok; i++ ) {

			uint16_t expected = test.final.has [ i ] ? test.final.regs [ i ] : test.initial.regs [ i ];
			uint16_t actual = register_slot ( r, i );

			if ( i == 13 ) {

				expected = static_cast < uint16_t > ( expected & flags_mask );
				actual = static_cast < uint16_t > ( actual & flags_mask );

			}

			if ( expected != actual ) {

				ok = false;
				append ( problem, room, register_names [ i ] );
				append ( problem, room, " expected " );
				append_hex ( problem, room, expected, 4 );
				append ( problem, room, " got " );
				append_hex ( problem, room, actual, 4 );

			}

		}

		for ( int32_t i = 0; i < test.final.addresses.count ( ) && ok; i++ ) {

			uint32_t address = test.final.addresses [ static_cast < size_t > ( i ) ];
			uint8_t expected = test.final.values [ static_cast < size_t > ( i ) ];
			uint8_t actual = memory.read8 ( address );

			if ( expected != actual ) {

				ok = false;
				append ( problem, room, "memory " );
				append_hex ( problem, room, address, 5 );
				append ( problem, room, " expected " );
				append_hex ( problem, room, expected, 2 );
				append ( problem, room, " got " );
				append_hex ( problem, room, actual, 2 );

			}

		}

		for ( int32_t w = 0; w < writes.count ( ) && ok; w++ ) {

			uint32_t address = writes [ static_cast < size_t > ( w ) ];
			bool listed = false;

			for ( int32_t i = 0; i < test.final.addresses.count ( ) && !listed; i++ )
				listed = test.final.addresses [ static_cast < size_t > ( i ) ] == address;

			if ( listed )
				continue;

			uint8_t original = 0;

			for ( int32_t i = 0; i < test.initial.addresses.count ( ); i++ ) {

				if ( test.initial.addresses [ static_cast < size_t > ( i ) ] == address )
					original = test.initial.values [ static_cast < size_t > ( i ) ];

			}

			if ( memory.read8 ( address ) != original ) {

				ok = false;
				append ( problem, room, "unexpected write at " );
				append_hex ( problem, room, address, 5 );

			}

		}

		for ( int32_t i = 0; i < test.initial.addresses.count ( ); i++ )
			memory.write8 ( test.initial.addresses [ static_cast < size_t > ( i ) ], 0 );

		for ( uint32_t address : writes )
			memory.write8 ( address, 0 );

		return ok;

	}

	static void run_file ( machine::cpu& computer, file_job& job ) {

		base::list < char > packed;

		if ( !files::read ( job.path, packed ) ) {

			job.broken = true;

			return;

		}

		base::list < uint8_t > unpacked;

		if ( !inflate::gzip ( reinterpret_cast < const uint8_t* > ( packed.data ( ) ), static_cast < size_t > ( packed.count ( ) - 1 ), unpacked ) ) {

			job.broken = true;

			return;

		}

		json in;

		in.at = reinterpret_cast < const char* > ( unpacked.data ( ) );
		in.end = in.at + unpacked.count ( );
		in.need ( '[' );

		uint16_t mask = job.opcode >= 0 ? metadata [ job.opcode ].mask [ job.slot ] : 0xFFFF;

		if ( job.slot != 8 && job.opcode >= 0 && metadata [ job.opcode ].mask [ job.slot ] == 0xFFFF )
			mask = metadata [ job.opcode ].mask [ 8 ];

		if ( exact_flags )
			mask = 0xFFFF;

		test_case* test = new test_case ( );

		while ( !in.failed ) {

			if ( in.consume ( ']' ) )
				break;

			if ( !parse_test ( in, *test ) )
				break;

			job.total++;

			char problem [ 300 ];
			uint64_t before = computer.cycles;
			bool ok = run_case ( computer, *test, mask, problem, 300 );

			int64_t ours = static_cast < int64_t > ( computer.cycles - before );
			int64_t gap = ours > test->clocks ? ours - test->clocks : test->clocks - ours;

			job.our_clocks += ours;
			job.real_clocks += test->clocks;

			if ( gap * 5 <= test->clocks )
				job.close++;

			if ( ok )
				job.passed++;
			else if ( job.problem_count < 3 ) {

				char* line = job.problems [ job.problem_count++ ];

				line [ 0 ] = 0;
				append ( line, 400, "#" );
				append_number ( line, 400, test->index );
				append ( line, 400, " " );
				append ( line, 400, test->name );
				append ( line, 400, ": " );
				append ( line, 400, problem );

			}

			in.consume ( ',' );

		}

		if ( in.failed )
			job.broken = true;

		delete test;

	}

	static DWORD __stdcall worker ( void* ) {

		machine::bus* memory = new machine::bus ( );
		machine::cpu* computer = new machine::cpu ( *memory );

		computer->reset ( );

		while ( true ) {

			long index = InterlockedIncrement ( &next_job );

			if ( index >= jobs.count ( ) )
				break;

			file_job& job = jobs [ static_cast < size_t > ( index ) ];

			if ( !job.skipped )
				run_file ( *computer, job );

		}

		delete computer;
		delete memory;

		return 0;

	}

	int32_t single_step_suite ( const wchar_t* folder, const wchar_t* only, bool exact, bool timing ) {

		exact_flags = exact;
		show_timing = timing;

		if ( !load_metadata ( folder ) ) {

			print ( "metadata.json could not be read\r\n" );

			return 1;

		}

		base::writer pattern;
		WIN32_FIND_DATAW found = { };

		pattern.add ( folder ).add ( L"\\*.json.gz" );

		HANDLE search = FindFirstFileW ( pattern.text ( ), &found );

		if ( search == INVALID_HANDLE_VALUE ) {

			print ( "no test files found\r\n" );

			return 1;

		}

		do {

			if ( only && only [ 0 ] ) {

				bool match = true;

				for ( int32_t i = 0; only [ i ]; i++ ) {

					if ( found.cFileName [ i ] != only [ i ] ) {

						match = false;

						break;

					}

				}

				if ( !match )
					continue;

			}

			file_job& job = jobs.emplace ( );
			base::writer full;

			full.add ( folder ).add ( L"\\" ).add ( found.cFileName );

			for ( int32_t i = 0; full.text ( ) [ i ] && i < 599; i++ )
				job.path [ i ] = full.text ( ) [ i ];

			int32_t k = 0;

			for ( ; found.cFileName [ k ] && !( found.cFileName [ k ] == L'.' && found.cFileName [ k + 1 ] == L'j' ) && k < 31; k++ )
				job.label [ k ] = static_cast < char > ( found.cFileName [ k ] );

			job.label [ k ] = 0;
			job.opcode = hex_digit ( found.cFileName [ 0 ] ) * 16 + hex_digit ( found.cFileName [ 1 ] );

			if ( found.cFileName [ 2 ] == L'.' && found.cFileName [ 3 ] >= L'0' && found.cFileName [ 3 ] <= L'7' && found.cFileName [ 4 ] == L'.' )
				job.slot = found.cFileName [ 3 ] - L'0';

			if ( job.opcode >= 0 && job.opcode < 256 )
				job.skipped = metadata [ job.opcode ].skip [ job.slot ] || ( job.slot != 8 && metadata [ job.opcode ].skip [ 8 ] );

		} while ( FindNextFileW ( search, &found ) );

		FindClose ( search );

		SYSTEM_INFO system = { };

		GetSystemInfo ( &system );

		int32_t thread_count = static_cast < int32_t > ( base::clamp ( system.dwNumberOfProcessors, static_cast < DWORD > ( 1 ), static_cast < DWORD > ( 32 ) ) );
		HANDLE threads [ 32 ];
		double started = base::seconds ( );

		for ( int32_t i = 0; i < thread_count; i++ )
			threads [ i ] = CreateThread ( nullptr, 0, &worker, nullptr, 0, nullptr );

		WaitForMultipleObjects ( static_cast < DWORD > ( thread_count ), threads, TRUE, INFINITE );

		for ( int32_t i = 0; i < thread_count; i++ )
			CloseHandle ( threads [ i ] );

		int64_t total = 0;
		int64_t passed = 0;
		int64_t our_clocks = 0;
		int64_t real_clocks = 0;
		int64_t close = 0;
		int32_t clean_files = 0;
		int32_t skipped_files = 0;

		print ( "SingleStepTests 8088\r\n\r\n" );

		for ( file_job& job : jobs ) {

			if ( job.skipped ) {

				skipped_files++;

				continue;

			}

			total += job.total;
			passed += job.passed;
			our_clocks += job.our_clocks;
			real_clocks += job.real_clocks;
			close += job.close;

			if ( show_timing && job.real_clocks ) {

				char line [ 200 ] = { };

				append ( line, 200, "  " );
				append ( line, 200, job.label );
				append ( line, 200, "  clocks " );
				append_number ( line, 200, job.our_clocks / base::max ( job.total, 1 ) );
				append ( line, 200, " vs " );
				append_number ( line, 200, job.real_clocks / base::max ( job.total, 1 ) );
				append ( line, 200, " on hardware, " );
				append_number ( line, 200, job.our_clocks * 100 / job.real_clocks );
				append ( line, 200, "%\r\n" );
				print ( line );

			}

			if ( job.passed == job.total && !job.broken ) {

				clean_files++;

				continue;

			}

			char line [ 200 ] = { };

			append ( line, 200, "  " );
			append ( line, 200, job.label );
			append ( line, 200, job.broken ? "  could not be read fully, " : "  " );
			append_number ( line, 200, job.passed );
			append ( line, 200, "/" );
			append_number ( line, 200, job.total );
			append ( line, 200, "\r\n" );
			print ( line );

			for ( int32_t i = 0; i < job.problem_count; i++ ) {

				print ( "      " );
				print ( job.problems [ i ] );
				print ( "\r\n" );

			}

		}

		char summary [ 300 ] = { };

		append ( summary, 300, "\r\n" );
		append_number ( summary, 300, passed );
		append ( summary, 300, " of " );
		append_number ( summary, 300, total );
		append ( summary, 300, " tests passed, " );
		append_number ( summary, 300, clean_files );
		append ( summary, 300, " files fully clean, " );
		append_number ( summary, 300, skipped_files );
		append ( summary, 300, " files skipped as undefined, " );
		append_number ( summary, 300, static_cast < int64_t > ( ( base::seconds ( ) - started ) * 1000.0 ) );
		append ( summary, 300, " ms\r\nEstimated clocks come to " );
		append_number ( summary, 300, real_clocks ? our_clocks * 100 / real_clocks : 0 );
		append ( summary, 300, "% of the recorded hardware total, " );
		append_number ( summary, 300, total ? close * 100 / total : 0 );
		append ( summary, 300, "% of instructions within 20%\r\n" );
		print ( summary );

		return passed == total ? 0 : 1;

	}

}

#endif