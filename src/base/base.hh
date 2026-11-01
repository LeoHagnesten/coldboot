#pragma once

#include "runtime/runtime.hh"

typedef __INT8_TYPE__ int8_t;
typedef __INT16_TYPE__ int16_t;
typedef __INT32_TYPE__ int32_t;
typedef __INT64_TYPE__ int64_t;
typedef __UINT8_TYPE__ uint8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT64_TYPE__ uint64_t;
typedef runtime_size size_t;

namespace base {

	template < typename type >
	struct strip_reference {

		typedef type result;

	};

	template < typename type >
	struct strip_reference < type& > {

		typedef type result;

	};

	template < typename type >
	struct strip_reference < type&& > {

		typedef type result;

	};

	template < typename type >
	constexpr typename strip_reference < type >::result&& move ( type&& value ) {

		return static_cast < typename strip_reference < type >::result&& > ( value );

	}

	template < typename type >
	constexpr type&& forward ( typename strip_reference < type >::result& value ) {

		return static_cast < type&& > ( value );

	}

	template < typename type >
	inline void swap ( type& a, type& b ) {

		type held = move ( a );

		a = move ( b );
		b = move ( held );

	}

	template < typename type >
	constexpr type min ( type a, type b ) {

		return a < b ? a : b;

	}

	template < typename type >
	constexpr type max ( type a, type b ) {

		return a > b ? a : b;

	}

	template < typename type >
	constexpr type clamp ( type value, type low, type high ) {

		return value < low ? low : ( value > high ? high : value );

	}

	inline int32_t text_length ( const wchar_t* text ) {

		int32_t length = 0;

		while ( text && text [ length ] )
			length++;

		return length;

	}

	inline int32_t text_length ( const char* text ) {

		int32_t length = 0;

		while ( text && text [ length ] )
			length++;

		return length;

	}

	inline bool same_text ( const char* a, const char* b ) {

		while ( *a && *a == *b ) {

			a++;
			b++;

		}

		return *a == *b;

	}

	inline bool same_text ( const wchar_t* a, const wchar_t* b ) {

		while ( *a && *a == *b ) {

			a++;
			b++;

		}

		return *a == *b;

	}

	template < typename type >
	class list {

	public:

		list ( ) = default;

		list ( const list& other ) {

			copy_from ( other );

		}

		list ( list&& other ) noexcept : items ( other.items ), length ( other.length ), room ( other.room ) {

			other.items = nullptr;
			other.length = 0;
			other.room = 0;

		}

		list& operator= ( const list& other ) {

			if ( this != &other ) {

				clear ( );
				copy_from ( other );

			}

			return *this;

		}

		list& operator= ( list&& other ) noexcept {

			if ( this != &other ) {

				release ( );
				items = other.items;
				length = other.length;
				room = other.room;
				other.items = nullptr;
				other.length = 0;
				other.room = 0;

			}

			return *this;

		}

		~list ( ) {

			release ( );

		}

		void reserve ( size_t wanted ) {

			if ( wanted <= room )
				return;

			type* fresh = static_cast < type* > ( runtime_allocate ( sizeof ( type ) * wanted ) );

			for ( size_t i = 0; i < length; i++ ) {

				new ( &fresh [ i ] ) type ( move ( items [ i ] ) );
				items [ i ].~type ( );

			}

			runtime_release ( items );
			items = fresh;
			room = wanted;

		}

		void push ( const type& value ) {

			if ( length == room ) {

				type held = value;

				grow ( );
				new ( &items [ length++ ] ) type ( move ( held ) );

				return;

			}

			new ( &items [ length++ ] ) type ( value );

		}

		void push ( type&& value ) {

			if ( length == room )
				grow ( );

			new ( &items [ length++ ] ) type ( move ( value ) );

		}

		type& emplace ( ) {

			if ( length == room )
				grow ( );

			new ( &items [ length ] ) type ( );

			return items [ length++ ];

		}

		void resize ( size_t count ) {

			if ( count > length ) {

				reserve ( count );

				for ( size_t i = length; i < count; i++ )
					new ( &items [ i ] ) type ( );

			} else {

				for ( size_t i = count; i < length; i++ )
					items [ i ].~type ( );

			}

			length = count;

		}

		void fill ( size_t count, const type& value ) {

			clear ( );
			reserve ( count );

			for ( size_t i = 0; i < count; i++ )
				new ( &items [ i ] ) type ( value );

			length = count;

		}

		void remove_at ( size_t index ) {

			if ( index >= length )
				return;

			for ( size_t i = index + 1; i < length; i++ )
				items [ i - 1 ] = move ( items [ i ] );

			items [ --length ].~type ( );

		}

		void pop ( ) {

			if ( length )
				items [ --length ].~type ( );

		}

		void clear ( ) {

			for ( size_t i = 0; i < length; i++ )
				items [ i ].~type ( );

			length = 0;

		}

		type& operator[] ( size_t index ) {

			return items [ index ];

		}

		const type& operator[] ( size_t index ) const {

			return items [ index ];

		}

		type& back ( ) {

			return items [ length - 1 ];

		}

		const type& back ( ) const {

			return items [ length - 1 ];

		}

		type* data ( ) {

			return items;

		}

		const type* data ( ) const {

			return items;

		}

		type* begin ( ) {

			return items;

		}

		type* end ( ) {

			return items + length;

		}

		const type* begin ( ) const {

			return items;

		}

		const type* end ( ) const {

			return items + length;

		}

		size_t size ( ) const {

			return length;

		}

		int32_t count ( ) const {

			return static_cast < int32_t > ( length );

		}

		bool empty ( ) const {

			return length == 0;

		}

	private:

		void grow ( ) {

			reserve ( room < 8 ? 8 : room * 2 );

		}

		void copy_from ( const list& other ) {

			reserve ( other.length );

			for ( size_t i = 0; i < other.length; i++ )
				new ( &items [ i ] ) type ( other.items [ i ] );

			length = other.length;

		}

		void release ( ) {

			clear ( );
			runtime_release ( items );
			items = nullptr;
			room = 0;

		}

		type* items = nullptr;
		size_t length = 0;
		size_t room = 0;

	};

	template < typename type, typename less >
	void sort ( type* items, int32_t count, less before ) {

		for ( int32_t i = 1; i < count; i++ ) {

			type held = move ( items [ i ] );
			int32_t j = i - 1;

			while ( j >= 0 && before ( held, items [ j ] ) ) {

				items [ j + 1 ] = move ( items [ j ] );
				j--;

			}

			items [ j + 1 ] = move ( held );

		}

	}

	class writer {

	public:

		writer ( ) {

			buffer [ 0 ] = 0;

		}

		writer& add ( const wchar_t* value );

		writer& add ( const char* value );

		writer& add ( wchar_t value );

		writer& number ( int64_t value );

		writer& hex ( uint64_t value, int32_t digits = 0 );

		writer& grouped ( uint64_t value );

		writer& fixed ( double value, int32_t decimals );

		writer& signed_fixed ( double value, int32_t decimals );

		void clear ( ) {

			length = 0;
			buffer [ 0 ] = 0;

		}

		const wchar_t* text ( ) const {

			return buffer;

		}

		int32_t size ( ) const {

			return length;

		}

		static constexpr int32_t capacity = 2048;

	private:

		wchar_t buffer [ capacity ];
		int32_t length = 0;

	};

	class text {

	public:

		text ( ) = default;

		text ( const wchar_t* value ) {

			set ( value );

		}

		void set ( const wchar_t* value ) {

			chars.clear ( );

			for ( int32_t i = 0; value && value [ i ]; i++ )
				chars.push ( value [ i ] );

			chars.push ( 0 );

		}

		const wchar_t* get ( ) const {

			return chars.empty ( ) ? L"" : chars.data ( );

		}

		int32_t size ( ) const {

			return chars.empty ( ) ? 0 : chars.count ( ) - 1;

		}

		bool empty ( ) const {

			return size ( ) == 0;

		}

	private:

		list < wchar_t > chars;

	};

	double seconds ( );

	uint64_t ticks ( );

	void pause ( int32_t milliseconds );

	class random {

	public:

		void seed ( uint64_t value );

		uint64_t next ( );

		float unit ( );

	private:

		uint64_t state = 0x9E3779B97F4A7C15ull;

	};

}