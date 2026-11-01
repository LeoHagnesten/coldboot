#include "chipset/dma.hh"

namespace machine {

	void dma::reset ( ) {

		command = 0;
		status = 0;
		request = 0;
		masks = 0x0F;
		temporary = 0;
		flip = false;

	}

	bool dma::ready ( int32_t index ) const {

		return !( command & 0x04 ) && !( masks & ( 1 << index ) );

	}

	dma_result dma::transfer ( int32_t index, bus& memory, uint8_t& value ) {

		if ( !ready ( index ) )
			return DMA_BLOCKED;

		dma_channel& channel = channels [ index ];
		uint32_t address = static_cast < uint32_t > ( channel.page & 0x0F ) << 16 | channel.address;

		switch ( ( channel.mode >> 2 ) & 3 ) {

			case 1:
				memory.write8 ( address, value );
				break;

			case 2:
				value = memory.read8 ( address );
				break;

			default:
				break;

		}

		channel.address = static_cast < uint16_t > ( channel.mode & 0x20 ? channel.address - 1 : channel.address + 1 );

		if ( channel.count-- != 0 )
			return DMA_MOVED;

		status = static_cast < uint8_t > ( status | ( 1 << index ) );
		request = static_cast < uint8_t > ( request & ~( 1 << index ) );

		if ( channel.mode & 0x10 ) {

			channel.address = channel.base_address;
			channel.count = channel.base_count;

		} else
			masks = static_cast < uint8_t > ( masks | ( 1 << index ) );

		return DMA_TERMINAL;

	}

	uint8_t dma::in ( uint16_t port ) {

		uint8_t index = static_cast < uint8_t > ( port & 0x0F );

		if ( index < 8 ) {

			dma_channel& channel = channels [ index >> 1 ];
			uint16_t value = index & 1 ? channel.count : channel.address;
			bool high = flip;

			flip = !flip;

			return static_cast < uint8_t > ( high ? value >> 8 : value );

		}

		switch ( index ) {

			case 0x08: {

				uint8_t value = static_cast < uint8_t > ( status | ( request << 4 ) );

				status = static_cast < uint8_t > ( status & 0xF0 );

				return value;

			}

			case 0x0D:
				return temporary;

			default:
				return 0xFF;

		}

	}

	void dma::out ( uint16_t port, uint8_t value ) {

		uint8_t index = static_cast < uint8_t > ( port & 0x0F );

		if ( index < 8 ) {

			dma_channel& channel = channels [ index >> 1 ];
			bool high = flip;

			flip = !flip;

			if ( index & 1 ) {

				channel.base_count = static_cast < uint16_t > ( high ? ( channel.base_count & 0x00FF ) | value << 8 : ( channel.base_count & 0xFF00 ) | value );
				channel.count = channel.base_count;

			} else {

				channel.base_address = static_cast < uint16_t > ( high ? ( channel.base_address & 0x00FF ) | value << 8 : ( channel.base_address & 0xFF00 ) | value );
				channel.address = channel.base_address;

			}

			return;

		}

		switch ( index ) {

			case 0x08:
				command = value;
				return;

			case 0x09:

				if ( value & 0x04 )
					request = static_cast < uint8_t > ( request | ( 1 << ( value & 3 ) ) );
				else
					request = static_cast < uint8_t > ( request & ~( 1 << ( value & 3 ) ) );

				return;

			case 0x0A:

				if ( value & 0x04 )
					masks = static_cast < uint8_t > ( masks | ( 1 << ( value & 3 ) ) );
				else
					masks = static_cast < uint8_t > ( masks & ~( 1 << ( value & 3 ) ) );

				return;

			case 0x0B:
				channels [ value & 3 ].mode = static_cast < uint8_t > ( value & 0xFC );
				return;

			case 0x0C:
				flip = false;
				return;

			case 0x0D:
				reset ( );
				return;

			case 0x0E:
				masks = 0;
				return;

			case 0x0F:
				masks = static_cast < uint8_t > ( value & 0x0F );
				return;

			default:
				return;

		}

	}

	uint8_t dma_pages::in ( uint16_t ) {

		return 0xFF;

	}

	void dma_pages::out ( uint16_t port, uint8_t value ) {

		static const uint8_t owners [ 4 ] = { 0, 2, 3, 1 };

		controller.channels [ owners [ port & 3 ] ].page = static_cast < uint8_t > ( value & 0x0F );

	}

}