#include "app/keys.hh"

namespace app {

	int32_t xt_scan_code ( WPARAM key, LPARAM extra ) {

		uint32_t scan = static_cast < uint32_t > ( extra >> 16 ) & 0xFF;
		bool extended = ( ( extra >> 24 ) & 1 ) != 0;

		if ( key == VK_PAUSE || key == VK_LWIN || key == VK_RWIN || key == VK_APPS )
			return -1;

		if ( extended ) {

			switch ( scan ) {

				case 0x1C:
				case 0x1D:
				case 0x35:
				case 0x37:
				case 0x38:
				case 0x45:
				case 0x47:
				case 0x48:
				case 0x49:
				case 0x4B:
				case 0x4D:
				case 0x4F:
				case 0x50:
				case 0x51:
				case 0x52:
				case 0x53:
					return static_cast < int32_t > ( scan );

				default:
					return -1;

			}

		}

		if ( !scan || scan > 0x53 )
			return -1;

		return static_cast < int32_t > ( scan );

	}

}