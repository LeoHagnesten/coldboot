#pragma once

#include "bus/bus.hh"

namespace machine {

	extern const uint8_t font_8x8 [ 256 ] [ 8 ];

	class cga : public port_device {

	public:

		static constexpr int32_t max_width = 1024;
		static constexpr int32_t max_height = 512;
		static constexpr uint32_t palette [ 16 ] = {

			0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
			0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,

		};

		explicit cga ( bus& memory );

		~cga ( );

		cga ( const cga& ) = delete;

		cga& operator= ( const cga& ) = delete;

		void reset ( );

		bool update ( uint64_t cycles );

		uint8_t status ( uint64_t cycles ) const;

		uint8_t in ( uint16_t port ) override;

		void out ( uint16_t port, uint8_t value ) override;

		const uint64_t* clock = nullptr;
		uint32_t* pixels = nullptr;
		int32_t width = 640;
		int32_t height = 200;
		uint64_t frames = 0;
		uint64_t now = 0;
		uint8_t crtc [ 18 ] = { };
		uint8_t selected = 0;
		uint8_t mode = 0;
		uint8_t color = 0;

	private:

		uint32_t character_dots ( ) const {

			return mode & 1 ? 8 : 16;

		}

		uint32_t line_dots ( ) const;

		uint32_t frame_lines ( ) const;

		void render ( );

		const uint8_t* vram = nullptr;
		uint64_t last_frame = 0;

	};

}