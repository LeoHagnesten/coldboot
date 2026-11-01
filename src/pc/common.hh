#pragma once

#include "base/base.hh"

namespace pc {

	struct platform_bus {

		virtual ~platform_bus ( ) = default;

		virtual void set_irq ( uint32_t irq, bool level ) = 0;

		virtual void request_reset ( ) = 0;

		virtual void request_power_off ( ) = 0;

		virtual void console ( char value ) = 0;

		virtual uint64_t now ( ) = 0;

	};

	uint64_t host_nanoseconds ( );

}