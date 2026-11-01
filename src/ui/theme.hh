#pragma once

#include "render/gpu.hh"

namespace ui {

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

			render::rgb ( 0x0E0F11 ),
			render::rgb ( 0x15161A ),
			render::rgb ( 0x24262C ),
			render::rgb ( 0x1B1D21 ),
			render::rgb ( 0x0F1013 ),
			render::rgb ( 0xFFFFFF, 0.07f ),
			render::rgb ( 0xFFFFFF, 0.12f ),
			render::rgb ( 0xECEDEF ),
			render::rgb ( 0x8D919A ),
			render::rgb ( 0x5A5E66 ),
			render::rgb ( 0xF0A848 ),
			render::rgb ( 0x1B1206 ),
			render::rgb ( 0x43D17A ),
			render::rgb ( 0xFF5340 ),
			render::rgb ( 0x3A3D44 ),
			render::rgb ( 0x050506 ),
			render::rgb ( 0x000000, 0.55f ),
			render::rgb ( 0xFF6B5B ),

		};

	}

	inline theme light_theme ( ) {

		return theme {

			render::rgb ( 0xE6E6E2 ),
			render::rgb ( 0xF8F8F6 ),
			render::rgb ( 0xDCDCD7 ),
			render::rgb ( 0xFFFFFF ),
			render::rgb ( 0xEEEEEB ),
			render::rgb ( 0x000000, 0.05f ),
			render::rgb ( 0x000000, 0.09f ),
			render::rgb ( 0x1A1B1E ),
			render::rgb ( 0x686B72 ),
			render::rgb ( 0xA4A7AD ),
			render::rgb ( 0xC57610 ),
			render::rgb ( 0xFFFFFF ),
			render::rgb ( 0x1FA75A ),
			render::rgb ( 0xE5402E ),
			render::rgb ( 0xC6C8CC ),
			render::rgb ( 0x0A0A0B ),
			render::rgb ( 0x000000, 0.22f ),
			render::rgb ( 0xD9412F ),

		};

	}

}