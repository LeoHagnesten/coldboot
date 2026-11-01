#pragma once

#include "chipset/pic.hh"
#include "chipset/pit.hh"
#include "hv/machine.hh"
#include "pc/common.hh"

namespace pc {

	class dual_pic : public hv::device, public hv::legacy_source {

	public:

		void reset ( );

		void set_line ( uint32_t irq, bool level );

		bool asserted ( ) override;

		uint8_t acknowledge ( ) override;

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		machine::pic master;
		machine::pic slave;

	private:

		void cascade ( );

	};

	class interval_timer : public hv::device {

	public:

		static constexpr uint64_t rate = 1193182;

		explicit interval_timer ( platform_bus& owner ) : bus ( owner ) { }

		void reset ( );

		void advance ( );

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		machine::pit chip;

	private:

		platform_bus& bus;
		uint64_t ticks = 0;
		uint64_t origin = 0;
		bool output = false;
		uint8_t control = 0;

	};

	class real_time_clock : public hv::device {

	public:

		explicit real_time_clock ( platform_bus& owner ) : bus ( owner ) { }

		void reset ( uint64_t memory_bytes );

		void advance ( );

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		uint8_t cmos [ 128 ] = { };
		int64_t offset = 0;

	private:

		int64_t seconds_now ( ) const;

		uint8_t encode ( int32_t value ) const;

		int32_t decode ( uint8_t value ) const;

		void refresh ( );

		void update_irq ( );

		platform_bus& bus;
		int64_t epoch_ticks = 0;
		uint64_t epoch_nanoseconds = 0;
		uint8_t index = 0;
		int64_t last_second = 0;
		uint64_t last_period = 0;
		bool line = false;

	};

	class system_ports : public hv::device {

	public:

		explicit system_ports ( platform_bus& owner ) : bus ( owner ) { }

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		uint8_t post_code = 0;
		uint8_t port92 = 0x02;

	private:

		platform_bus& bus;

	};

}