#pragma once

#include "base/base.hh"

namespace machine {

	class archive;

	struct port_device {

		virtual uint8_t in ( uint16_t port ) = 0;

		virtual void out ( uint16_t port, uint8_t value ) = 0;

	};

	enum page_kind : uint8_t {

		PAGE_OPEN,
		PAGE_RAM,
		PAGE_ROM,

	};

	class bus {

	public:

		static constexpr uint32_t memory_size = 0x110000;
		static constexpr uint32_t page_size = 0x1000;
		static constexpr uint32_t page_count = memory_size / page_size;

		bus ( );

		~bus ( );

		bus ( const bus& ) = delete;

		bus& operator= ( const bus& ) = delete;

		uint8_t read8 ( uint32_t address ) const {

			uint32_t at = mask ( address );
			uint32_t page = at / page_size;

			return kinds [ page ] == PAGE_OPEN ? 0xFF : memory [ bases [ page ] + at % page_size ];

		}

		void write8 ( uint32_t address, uint8_t value ) {

			uint32_t at = mask ( address );
			uint32_t page = at / page_size;

			if ( write_log )
				write_log->push ( at );

			if ( kinds [ page ] == PAGE_RAM )
				memory [ bases [ page ] + at % page_size ] = value;

		}

		uint16_t read16 ( uint32_t address ) const;

		void write16 ( uint32_t address, uint16_t value );

		void map ( uint32_t first, uint32_t size, page_kind kind, uint32_t backing );

		bool load_rom ( uint32_t base, const uint8_t* data, uint32_t size );

		void clear ( );

		void transfer ( archive& io );

		uint8_t* backing ( uint32_t offset ) {

			return memory + offset;

		}

		void attach ( uint16_t first, uint16_t last, port_device* device );

		uint8_t in8 ( uint16_t port );

		void out8 ( uint16_t port, uint8_t value );

		uint32_t mask ( uint32_t address ) const {

			return a20 ? address % memory_size : address & 0xFFFFF;

		}

		bool a20 = false;
		base::list < uint32_t >* write_log = nullptr;

	private:

		uint8_t* memory = nullptr;
		uint8_t kinds [ page_count ] = { };
		uint32_t bases [ page_count ] = { };
		port_device** ports = nullptr;

	};

}