#pragma once

#include "pc/ioapic.hh"
#include "pc/legacy.hh"
#include "pc/pci.hh"
#include "pc/peripherals.hh"

namespace pc {

	struct platform_options {

		uint32_t processors = 1;
		uint64_t memory = 256ull * 1024 * 1024;

	};

	class platform : public platform_bus {

	public:

		platform ( );

		~platform ( ) override;

		bool create ( const platform_options& options );

		void destroy ( );

		bool start ( );

		void stop ( );

		void set_irq ( uint32_t irq, bool level ) override;

		void request_reset ( ) override;

		void request_power_off ( ) override;

		void console ( char value ) override;

		uint64_t now ( ) override;

		int32_t console_text ( char* out, int32_t room );

		void load ( uint64_t address, const uint8_t* data, uint32_t size );

		void add_ticker ( ticking* target );

		hv::machine vm;
		dual_pic pics;
		interval_timer timer;
		real_time_clock clock;
		system_ports ports;
		keyboard_controller keyboard;
		power_management power;
		serial_port serial;
		pci_bus pci;
		pci_function host_bridge;
		pci_function isa_bridge;
		ioapic* router = nullptr;
		volatile long reset_requested = 0;
		volatile long power_off_requested = 0;

	private:

		static DWORD __stdcall timer_loop ( void* context );

		void tick ( );

		HANDLE timer_thread = nullptr;
		volatile long timer_quit = 0;
		ticking* tickers [ 8 ] = { };
		uint32_t ticker_count = 0;
		SRWLOCK console_lock = SRWLOCK_INIT;
		char log [ 65536 ] = { };
		int32_t log_length = 0;

	};

}