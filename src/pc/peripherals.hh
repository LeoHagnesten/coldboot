#pragma once

#include "pc/keyboard.hh"

namespace pc {

	class power_management : public hv::device {

	public:

		static constexpr uint16_t base = 0x600;
		static constexpr uint32_t sci = 9;
		static constexpr uint32_t sleep_off = 5;
		static constexpr uint64_t frequency = 3579545;

		explicit power_management ( platform_bus& owner ) : bus ( owner ) { }

		void reset ( );

		void advance ( );

		void press_power ( );

		uint32_t timer ( ) const;

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		uint16_t status = 0;
		uint16_t enable = 0;
		uint16_t control = 1;
		uint32_t gpe_status = 0;
		uint32_t gpe_enable = 0;

	private:

		uint8_t byte_at ( uint32_t offset ) const;

		void update ( );

		platform_bus& bus;
		uint64_t origin = 0;
		bool high_bit = false;
		bool line = false;

	};

	class serial_port : public hv::device {

	public:

		serial_port ( platform_bus& owner, uint16_t port, uint32_t irq ) : bus ( owner ), base ( port ), line_number ( irq ) { }

		void reset ( );

		void receive ( uint8_t value );

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

	private:

		void update ( );

		platform_bus& bus;
		uint16_t base;
		uint32_t line_number;
		byte_queue incoming;
		uint8_t interrupts = 0;
		uint8_t identity = 0x01;
		uint8_t format = 0x03;
		uint8_t modem = 0;
		uint8_t scratch = 0;
		uint8_t fifo = 0;
		uint8_t divisor [ 2 ] = { 12, 0 };
		bool empty_pending = false;
		bool line = false;

	};

}