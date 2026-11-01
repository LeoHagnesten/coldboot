#pragma once

#include "render/gpu.hh"

namespace ui {

	static constexpr float corner = 4.0f;
	static constexpr float corner_large = 6.0f;

	struct theme {

		render::color background;
		render::color chrome;
		render::color divider;
		render::color raised;
		render::color inset;
		render::color hover;
		render::color pressed;
		render::color text;
		render::color muted;
		render::color faint;
		render::color accent;
		render::color accent_text;
		render::color led_green;
		render::color led_red;
		render::color led_off;
		render::color bezel;
		render::color shadow;
		render::color danger;

	};

	inline theme dark_theme ( ) {

		return theme {

			render::rgb ( 0x111214 ),
			render::rgb ( 0x17181B ),
			render::rgb ( 0x27292E ),
			render::rgb ( 0x1D1F23 ),
			render::rgb ( 0x0C0D0F ),
			render::rgb ( 0xFFFFFF, 0.05f ),
			render::rgb ( 0xFFFFFF, 0.09f ),
			render::rgb ( 0xE4E5E7 ),
			render::rgb ( 0x979AA1 ),
			render::rgb ( 0x5F6269 ),
			render::rgb ( 0xD99B45 ),
			render::rgb ( 0x1A1307 ),
			render::rgb ( 0x3FBE72 ),
			render::rgb ( 0xF0513D ),
			render::rgb ( 0x3A3D43 ),
			render::rgb ( 0x050506 ),
			render::rgb ( 0x000000, 0.5f ),
			render::rgb ( 0xEC6656 ),

		};

	}

	inline theme light_theme ( ) {

		return theme {

			render::rgb ( 0xF7F7F6 ),
			render::rgb ( 0xEEEEEC ),
			render::rgb ( 0xDBDBD7 ),
			render::rgb ( 0xFFFFFF ),
			render::rgb ( 0xF3F3F1 ),
			render::rgb ( 0x000000, 0.045f ),
			render::rgb ( 0x000000, 0.08f ),
			render::rgb ( 0x1B1C1F ),
			render::rgb ( 0x62656C ),
			render::rgb ( 0xA2A5AB ),
			render::rgb ( 0xB46B0D ),
			render::rgb ( 0xFFFFFF ),
			render::rgb ( 0x1E9E55 ),
			render::rgb ( 0xD9412F ),
			render::rgb ( 0xC6C8CC ),
			render::rgb ( 0x0A0A0B ),
			render::rgb ( 0x000000, 0.16f ),
			render::rgb ( 0xC9392A ),

		};

	}

}