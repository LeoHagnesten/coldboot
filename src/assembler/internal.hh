#pragma once

#include "assembler/assembler.hh"

namespace assembler {

	enum token_kind : uint8_t {

		TOKEN_END,
		TOKEN_NAME,
		TOKEN_NUMBER,
		TOKEN_STRING,
		TOKEN_MARK,

	};

	struct token {

		const char* start = nullptr;
		int32_t length = 0;
		int64_t value = 0;
		token_kind kind = TOKEN_END;
		char mark = 0;

	};

	enum operand_kind : uint8_t {

		OPERAND_NONE,
		OPERAND_REGISTER,
		OPERAND_SEGMENT,
		OPERAND_IMMEDIATE,
		OPERAND_MEMORY,
		OPERAND_FAR,

	};

	struct operand {

		operand_kind kind = OPERAND_NONE;
		uint8_t number = 0;
		uint8_t size = 0;
		uint8_t rm = 0;
		uint8_t override = 0xFF;
		bool direct = false;
		bool known = true;
		bool far_hint = false;
		bool near_hint = false;
		bool short_hint = false;
		int32_t value = 0;
		int32_t segment_value = 0;

	};

	struct statement {

		int32_t line = 0;
		int32_t first = 0;
		int32_t count = 0;
		int32_t scope = -1;
		uint32_t address = 0;
		uint32_t size = 0;
		bool wide = false;
		bool placed = false;

	};

	struct definition {

		int32_t name = 0;
		int32_t length = 0;
		int32_t statement = -1;
		int32_t first = 0;
		int32_t count = 0;
		int32_t scope = -1;
		bool resolving = false;

	};

	struct emitter {

		uint8_t bytes [ 32 ] = { };
		int32_t size = 0;

		void byte ( uint32_t value ) {

			if ( size < 32 )
				bytes [ size++ ] = static_cast < uint8_t > ( value );

		}

		void word ( uint32_t value ) {

			byte ( value );
			byte ( value >> 8 );

		}

	};

	class program {

	public:

		static constexpr int32_t table_size = 8192;

		bool run ( const char* text, int32_t length, const blob* blob_list, int32_t blob_total, result& out );

		bool fail ( const char* message );

		bool fail ( const char* message, const token& detail );

		bool same ( const token& item, const char* text ) const;

		bool evaluate ( int32_t first, int32_t count, int64_t& value, bool& known );

		bool split ( int32_t first, int32_t count, int32_t* starts, int32_t* counts, int32_t& found, int32_t room );

		bool parse_operand ( int32_t first, int32_t count, operand& result );

		bool encode ( statement& item, int32_t first, int32_t count, emitter& out );

		bool small ( statement& item, bool fits );

		base::list < token > tokens;
		statement* current = nullptr;
		uint32_t origin = 0;
		int32_t active_scope = -1;
		bool final = false;
		result* output = nullptr;

	private:

		bool lex ( const char* text, int32_t length );

		bool collect ( );

		bool layout ( bool& changed );

		bool place ( statement& item, uint32_t& here, bool& started, base::list < uint8_t >* bytes );

		bool data ( int32_t first, int32_t count, int32_t unit, int64_t repeat, base::list < uint8_t >* bytes, uint32_t& size );

		uint32_t hash ( const char* text, int32_t length ) const;

		int32_t find_name ( const char* text, int32_t length ) const;

		int32_t resolve ( const token& item, int32_t scope ) const;

		bool define ( const token& item, int32_t scope, const definition& entry );

		bool symbol_value ( int32_t index, int64_t& value, bool& known );

		bool expression ( int32_t& at, int32_t end, int32_t level, int64_t& value, bool& known );

		bool primary ( int32_t& at, int32_t end, int64_t& value, bool& known );

		base::list < statement > statements;
		base::list < definition > definitions;
		base::list < char > names;
		base::list < int32_t > table;
		const blob* blobs = nullptr;
		int32_t blob_count = 0;
		int32_t failing_line = 0;

	};

}