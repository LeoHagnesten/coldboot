#pragma once

#include "hv/machine.hh"

namespace pc {

	class pci_function {

	public:

		virtual ~pci_function ( ) = default;

		void identify ( uint16_t vendor, uint16_t device, uint8_t base_class, uint8_t sub_class, uint8_t interface, uint8_t revision, uint8_t header );

		void set_bar ( int32_t index, uint32_t size, bool io, uint32_t address );

		void set_interrupt ( uint8_t pin, uint8_t line );

		uint32_t bar_address ( int32_t index ) const;

		uint32_t read_config ( uint32_t offset, uint32_t size ) const;

		void write_config ( uint32_t offset, uint32_t size, uint32_t value );

		virtual void written ( uint32_t ) { }

		uint8_t config [ 256 ] = { };
		uint32_t bar_masks [ 6 ] = { };

	};

	class pci_bus : public hv::device {

	public:

		void attach ( uint32_t device, uint32_t function, pci_function* target );

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		uint32_t address = 0;

	private:

		pci_function* selected ( ) const;

		pci_function* slots [ 32 ] [ 8 ] = { };

	};

}