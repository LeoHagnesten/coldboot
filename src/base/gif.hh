#pragma once

#include "base/base.hh"

namespace gif {

	class writer {

	public:

		writer ( ) = default;

		writer ( const writer& ) = delete;

		writer& operator= ( const writer& ) = delete;

		~writer ( );

		bool open ( const wchar_t* path, int32_t width, int32_t height );

		void frame ( const uint32_t* pixels, int32_t source_width, int32_t source_height, uint32_t delay, bool stretch );

		bool close ( );

		int32_t frames = 0;

	private:

		void scale ( const uint32_t* pixels, int32_t source_width, int32_t source_height, bool stretch );

		void encode ( int32_t left, int32_t top, int32_t right, int32_t bottom );

		void flush ( );

		void put ( const void* data, uint32_t size );

		void* file = nullptr;
		int32_t width = 0;
		int32_t height = 0;
		uint32_t* current = nullptr;
		uint32_t* shown = nullptr;
		bool first = true;
		base::list < uint8_t > held;
		uint32_t held_delay = 0;
		uint32_t held_delay_at = 0;
		bool failed = false;

	};

}