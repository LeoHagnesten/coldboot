#include "pc/platform.hh"

namespace pc {

	platform::platform ( ) : timer ( *this ), clock ( *this ), ports ( *this ), keyboard ( *this ), power ( *this ), serial ( *this, 0x3F8, 4 ) {

	}

	platform::~platform ( ) {

		destroy ( );

	}

	bool platform::create ( const platform_options& options ) {

		destroy ( );

		if ( !vm.create ( options.processors, options.memory ) )
			return false;

		router = new ioapic ( vm );
		vm.attach_ports ( 0x20, 0x21, &pics );
		vm.attach_ports ( 0xA0, 0xA1, &pics );
		vm.attach_ports ( 0x4D0, 0x4D1, &pics );
		vm.attach_ports ( 0x40, 0x43, &timer );
		vm.attach_ports ( 0x61, 0x61, &timer );
		vm.attach_ports ( 0x70, 0x71, &clock );
		vm.attach_ports ( 0x60, 0x60, &keyboard );
		vm.attach_ports ( 0x64, 0x64, &keyboard );
		vm.attach_ports ( 0x80, 0x80, &ports );
		vm.attach_ports ( 0x92, 0x92, &ports );
		vm.attach_ports ( 0xE9, 0xE9, &ports );
		vm.attach_ports ( power_management::base, power_management::base + 0x27, &power );
		vm.attach_ports ( 0x3F8, 0x3FF, &serial );
		vm.attach_ports ( 0xCF8, 0xCFF, &pci );
		vm.attach_memory ( ioapic::base, ioapic::window, router );
		vm.set_legacy ( &pics );
		vm.listen_eoi ( router );
		host_bridge.identify ( 0x8086, 0x1237, 0x06, 0x00, 0x00, 0x02, 0x00 );
		isa_bridge.identify ( 0x8086, 0x7000, 0x06, 0x01, 0x00, 0x00, 0x80 );

		for ( uint32_t i = 0x60; i < 0x64; i++ )
			isa_bridge.config [ i ] = 0x80;

		pci.attach ( 0, 0, &host_bridge );
		pci.attach ( 1, 0, &isa_bridge );
		pics.reset ( );
		timer.reset ( );
		clock.reset ( options.memory );
		keyboard.reset ( );
		power.reset ( );
		serial.reset ( );
		vm.virtual_wire ( 0 );

		for ( uint32_t i = 1; i < vm.core.processors; i++ )
			vm.park ( i );

		return true;

	}

	void platform::destroy ( ) {

		stop ( );
		vm.destroy ( );
		delete router;
		router = nullptr;
		log_length = 0;
		reset_requested = 0;
		power_off_requested = 0;

	}

	bool platform::start ( ) {

		timer_quit = 0;
		timer_thread = CreateThread ( nullptr, 0, &timer_loop, this, 0, nullptr );

		return timer_thread && vm.start ( );

	}

	void platform::stop ( ) {

		vm.stop ( );

		if ( timer_thread ) {

			InterlockedExchange ( &timer_quit, 1 );
			WaitForSingleObject ( timer_thread, INFINITE );
			CloseHandle ( timer_thread );
			timer_thread = nullptr;

		}

	}

	void platform::set_irq ( uint32_t irq, bool level ) {

		pics.set_line ( irq, level );
		vm.legacy_changed ( );

		if ( router )
			router->set_line ( irq == 0 ? 2 : irq, level );

	}

	void platform::request_reset ( ) {

		InterlockedExchange ( &reset_requested, 1 );

	}

	void platform::request_power_off ( ) {

		InterlockedExchange ( &power_off_requested, 1 );

	}

	void platform::console ( char value ) {

		AcquireSRWLockExclusive ( &console_lock );

		if ( log_length < static_cast < int32_t > ( sizeof ( log ) ) - 1 )
			log [ log_length++ ] = value;

		ReleaseSRWLockExclusive ( &console_lock );

	}

	uint64_t platform::now ( ) {

		return host_nanoseconds ( );

	}

	int32_t platform::console_text ( char* out, int32_t room ) {

		AcquireSRWLockShared ( &console_lock );

		int32_t count = log_length < room - 1 ? log_length : room - 1;

		for ( int32_t i = 0; i < count; i++ )
			out [ i ] = log [ i ];

		out [ count ] = 0;
		ReleaseSRWLockShared ( &console_lock );

		return count;

	}

	void platform::load ( uint64_t address, const uint8_t* data, uint32_t size ) {

		uint8_t* memory = vm.memory ( );

		for ( uint32_t i = 0; memory && i < size && address + i < vm.core.memory_size; i++ )
			memory [ address + i ] = data [ i ];

	}

	DWORD __stdcall platform::timer_loop ( void* context ) {

		platform* self = static_cast < platform* > ( context );
		HANDLE waiter = CreateWaitableTimerExW ( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
		LARGE_INTEGER due;

		due.QuadPart = -10000;

		if ( waiter )
			SetWaitableTimer ( waiter, &due, 1, nullptr, nullptr, FALSE );

		while ( !self->timer_quit ) {

			if ( waiter )
				WaitForSingleObject ( waiter, 5 );
			else
				Sleep ( 1 );

			self->tick ( );

		}

		if ( waiter )
			CloseHandle ( waiter );

		return 0;

	}

	void platform::tick ( ) {

		vm.lock_devices ( );
		timer.advance ( );
		clock.advance ( );
		power.advance ( );
		vm.unlock_devices ( );

	}

}