#pragma once

#include "base/base.hh"
#include <windows.h>

namespace app {

	class audio {

	public:

		static constexpr uint32_t ring_size = 32768;

		bool start ( );

		void stop ( );

		void push ( const float* samples, int32_t count );

		void push_stereo ( const float* frames, int32_t count, uint32_t source_rate );

		uint32_t rate = 0;

	private:

		static DWORD __stdcall loop ( void* self );

		void run ( );

		int32_t take ( float* out, int32_t count );

		int32_t take_stereo ( float* out, int32_t frames );

		float ring [ ring_size ] = { };
		uint32_t head = 0;
		uint32_t count = 0;
		float stereo [ ring_size * 2 ] = { };
		uint32_t stereo_head = 0;
		uint32_t stereo_count = 0;
		float previous [ 2 ] = { };
		double phase = 0.0;
		SRWLOCK lock = SRWLOCK_INIT;
		HANDLE thread = nullptr;
		HANDLE ready = nullptr;
		volatile long quit = 0;

	};

}