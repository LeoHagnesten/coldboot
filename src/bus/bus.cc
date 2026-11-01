#include "bus/bus.hh"

namespace machine {

	bus::bus ( ) {

		memory = static_cast < uint8_t* > ( runtime_allocate_zero ( memory_size ) );
		ports = static_cast < port_device** > ( runtime_allocate_zero ( sizeof ( port_device* ) * 0x10000 ) );
		map ( 0, memory_size, PAGE_RAM, 0 );

	}

	bus::~bus ( ) {

		runtime_release ( memory );
		runtime_release ( ports );

	}

	uint16_t bus::read16 ( uint32_t address ) const {

		return static_cast < uint16_t > ( read8 ( address ) | read8 ( address + 1 ) << 8 );

	}

	void bus::write16 ( uint32_t address, uint16_t value ) {

		write8 ( address, static_cast < uint8_t > ( value ) );
		write8 ( address + 1, static_cast < uint8_t > ( value >> 8 ) );

	}

	void bus::map ( uint32_t first, uint32_t size, page_kind kind, uint32_t backing ) {

		for ( uint32_t page = first / page_size; page < ( first + size + page_size - 1 ) / page_size && page < page_count; page++ ) {

			kinds [ page ] = kind;
			bases [ page ] = backing + ( page * page_size - first );

		}

	}

	bool bus::load_rom ( uint32_t base, const uint8_t* data, uint32_t size ) {

		if ( !data || !size || base % page_size || base + size > 0x100000 )
			return false;

		for ( uint32_t i = 0; i < size; i++ )
			memory [ base + i ] = data [ i ];

		map ( base, size, PAGE_ROM, base );

		return true;

	}

	void bus::clear ( ) {

		for ( uint32_t page = 0; page < page_count; page++ ) {

			if ( kinds [ page ] != PAGE_RAM )
				continue;

			for ( uint32_t i = 0; i < page_size; i++ )
				memory [ bases [ page ] + i ] = 0;

		}

	}

	void bus::attach ( uint16_t first, uint16_t last, port_device* device ) {

		for ( uint32_t port = first; port <= last; port++ )
			ports [ port ] = device;

	}

	uint8_t bus::in8 ( uint16_t port ) {

		port_device* device = ports [ port ];

		return device ? device->in ( port ) : 0xFF;

	}

	void bus::out8 ( uint16_t port, uint8_t value ) {

		port_device* device = ports [ port ];

		if ( device )
			device->out ( port, value );

	}

}