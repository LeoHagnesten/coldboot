#pragma once

#include "bus/bus.hh"

namespace machine {

	enum dma_result : uint8_t {

		DMA_BLOCKED,
		DMA_MOVED,
		DMA_TERMINAL,

	};

	struct dma_channel {

		uint16_t base_address = 0;
		uint16_t address = 0;
		uint16_t base_count = 0;
		uint16_t count = 0;
		uint8_t mode = 0;
		uint8_t page = 0;

	};

	class dma : public port_device {

	public:

		void reset ( );

		bool ready ( int32_t index ) const;

		dma_result transfer ( int32_t index, bus& memory, uint8_t& value );

		uint8_t in ( uint16_t port ) override;

		void out ( uint16_t port, uint8_t value ) override;

		void transfer ( archive& io );

		dma_channel channels [ 4 ];
		uint8_t command = 0;
		uint8_t status = 0;
		uint8_t request = 0;
		uint8_t masks = 0x0F;
		uint8_t temporary = 0;
		bool flip = false;

	};

	class dma_pages : public port_device {

	public:

		explicit dma_pages ( dma& owner ) : controller ( owner ) {

		}

		uint8_t in ( uint16_t port ) override;

		void out ( uint16_t port, uint8_t value ) override;

		uint8_t post_code = 0;

	private:

		dma& controller;

	};

}