#ifdef COLDBOOT_DEBUG

#include "assembler/assembler.hh"
#include "pc/platform.hh"
#include "tests/harness.hh"
#include <windows.h>

namespace tests {

	struct fake_board : pc::platform_bus {

		uint64_t time = 1000000000ull;
		uint32_t lines = 0;
		uint32_t rises [ 24 ] = { };
		bool reset_seen = false;
		bool off_seen = false;
		char text [ 64 ] = { };
		int32_t length = 0;

		void set_irq ( uint32_t irq, bool level ) override {

			uint32_t bit = 1u << irq;

			if ( level && !( lines & bit ) )
				rises [ irq ]++;

			lines = level ? lines | bit : lines & ~bit;

		}

		void request_reset ( ) override {

			reset_seen = true;

		}

		void request_power_off ( ) override {

			off_seen = true;

		}

		void console ( char value ) override {

			if ( length < 63 )
				text [ length++ ] = value;

		}

		uint64_t now ( ) override {

			return time;

		}

	};

	static void out8 ( hv::device& target, uint16_t port, uint8_t value ) {

		target.write ( port, 1, value );

	}

	static uint8_t in8 ( hv::device& target, uint16_t port ) {

		return static_cast < uint8_t > ( target.read ( port, 1 ) );

	}

	static void pic_tests ( ) {

		pc::dual_pic* pics = new pc::dual_pic ( );
		static const uint8_t master_init [ 4 ] = { 0x11, 0x08, 0x04, 0x01 };
		static const uint8_t slave_init [ 4 ] = { 0x11, 0x70, 0x02, 0x01 };

		pics->reset ( );

		for ( int32_t i = 0; i < 4; i++ ) {

			out8 ( *pics, i ? 0x21 : 0x20, master_init [ i ] );
			out8 ( *pics, i ? 0xA1 : 0xA0, slave_init [ i ] );

		}

		out8 ( *pics, 0x21, 0x00 );
		out8 ( *pics, 0xA1, 0x00 );
		pics->set_line ( 10, true );

		bool raised = pics->asserted ( );
		uint8_t vector = pics->acknowledge ( );
		bool nested = ( pics->master.isr & 0x04 ) && ( pics->slave.isr & 0x04 ) && !pics->asserted ( );

		out8 ( *pics, 0xA0, 0x20 );
		out8 ( *pics, 0x20, 0x20 );
		pics->set_line ( 10, false );

		expect ( raised && vector == 0x72 && nested && !pics->master.isr && !pics->slave.isr, "chipset: a slave PIC interrupt cascades through IRQ 2 to its vector" );

		out8 ( *pics, 0x4D1, 0x08 );
		pics->set_line ( 11, true );
		vector = pics->acknowledge ( );
		out8 ( *pics, 0xA0, 0x20 );
		out8 ( *pics, 0x20, 0x20 );

		bool held = pics->asserted ( );

		pics->set_line ( 11, false );

		bool dropped = !pics->asserted ( );

		pics->set_line ( 1, true );

		uint8_t edge = pics->acknowledge ( );

		out8 ( *pics, 0x20, 0x20 );

		bool once = !pics->asserted ( );

		expect ( vector == 0x73 && held && dropped && edge == 0x09 && once && in8 ( *pics, 0x4D1 ) == 0x08, "chipset: ELCR level lines stay asserted until released and edge lines fire once" );

		delete pics;

	}

	static void timer_tests ( ) {

		fake_board* board = new fake_board ( );
		pc::interval_timer* pit = new pc::interval_timer ( *board );

		pit->reset ( );
		out8 ( *pit, 0x43, 0x34 );
		out8 ( *pit, 0x40, 0xA9 );
		out8 ( *pit, 0x40, 0x04 );
		board->time += 10000000;
		pit->advance ( );

		uint32_t ticks = board->rises [ 0 ];

		out8 ( *pit, 0x43, 0x00 );

		uint32_t latched = in8 ( *pit, 0x40 );

		latched |= static_cast < uint32_t > ( in8 ( *pit, 0x40 ) ) << 8;

		expect ( ticks >= 9 && ticks <= 11 && latched <= 1193, "chipset: the PIT runs at 1.19 MHz against the clock and latches its count" );

		delete pit;
		delete board;

	}

	static void clock_tests ( ) {

		fake_board* board = new fake_board ( );
		pc::real_time_clock* rtc = new pc::real_time_clock ( *board );
		SYSTEMTIME local;

		rtc->reset ( 64ull * 1024 * 1024 );
		GetLocalTime ( &local );

		auto cmos = [ & ] ( uint8_t index ) {

			out8 ( *rtc, 0x70, index );

			return in8 ( *rtc, 0x71 );

		};

		auto bcd = [ ] ( uint8_t value ) {

			return ( value >> 4 ) * 10 + ( value & 15 );

		};

		int32_t minutes = bcd ( cmos ( 0x04 ) ) * 60 + bcd ( cmos ( 0x02 ) );
		int32_t expected = local.wHour * 60 + local.wMinute;
		bool close = minutes - expected <= 1 && expected - minutes <= 1;
		bool memory = cmos ( 0x30 ) == 0x00 && cmos ( 0x31 ) == 0xFC && cmos ( 0x34 ) == 0x00 && cmos ( 0x35 ) == 0x03;

		expect ( close && memory && cmos ( 0x0B ) == 0x02 && bcd ( cmos ( 0x09 ) ) == local.wYear % 100, "chipset: the CMOS clock reads the host time in BCD and reports memory sizes" );

		out8 ( *rtc, 0x70, 0x0B );
		out8 ( *rtc, 0x71, 0x42 );
		board->time += 3000000;
		rtc->advance ( );

		bool periodic = board->rises [ 8 ] >= 1 && ( board->lines & ( 1 << 8 ) );
		uint8_t flags = cmos ( 0x0C );

		expect ( periodic && ( flags & 0xC0 ) == 0xC0 && !( board->lines & ( 1 << 8 ) ), "chipset: the periodic interrupt raises IRQ 8 and reading register C clears it" );

		out8 ( *rtc, 0x70, 0x0B );
		out8 ( *rtc, 0x71, 0x82 );
		out8 ( *rtc, 0x70, 0x04 );
		out8 ( *rtc, 0x71, 0x13 );
		out8 ( *rtc, 0x70, 0x02 );
		out8 ( *rtc, 0x71, 0x37 );
		out8 ( *rtc, 0x70, 0x0B );
		out8 ( *rtc, 0x71, 0x02 );

		expect ( cmos ( 0x04 ) == 0x13 && cmos ( 0x02 ) == 0x37, "chipset: setting the clock with the SET bit sticks" );

		delete rtc;
		delete board;

	}

	static void keyboard_tests ( ) {

		fake_board* board = new fake_board ( );
		pc::keyboard_controller* kbc = new pc::keyboard_controller ( *board );

		kbc->reset ( );
		out8 ( *kbc, 0x64, 0xAA );

		bool self_test = in8 ( *kbc, 0x60 ) == 0x55;

		out8 ( *kbc, 0x60, 0xFF );

		bool reset = in8 ( *kbc, 0x60 ) == 0xFA && in8 ( *kbc, 0x60 ) == 0xAA;

		kbc->key ( 0x1E );

		bool ready = ( in8 ( *kbc, 0x64 ) & 1 ) && board->rises [ 1 ] >= 1;
		bool scan = in8 ( *kbc, 0x60 ) == 0x1E;

		expect ( self_test && reset && ready && scan, "chipset: the 8042 passes its self test, resets the keyboard and delivers keys on IRQ 1" );

		out8 ( *kbc, 0x64, 0xD4 );
		out8 ( *kbc, 0x60, 0xFF );

		bool mouse_reset = in8 ( *kbc, 0x60 ) == 0xFA && in8 ( *kbc, 0x60 ) == 0xAA && in8 ( *kbc, 0x60 ) == 0x00;
		static const uint8_t wheel [ 6 ] = { 0xF3, 200, 0xF3, 100, 0xF3, 80 };

		for ( uint8_t value : wheel ) {

			out8 ( *kbc, 0x64, 0xD4 );
			out8 ( *kbc, 0x60, value );
			in8 ( *kbc, 0x60 );

		}

		out8 ( *kbc, 0x64, 0xD4 );
		out8 ( *kbc, 0x60, 0xF2 );

		bool wheel_id = in8 ( *kbc, 0x60 ) == 0xFA && in8 ( *kbc, 0x60 ) == 0x03;

		out8 ( *kbc, 0x64, 0xD4 );
		out8 ( *kbc, 0x60, 0xF4 );
		in8 ( *kbc, 0x60 );
		kbc->mouse ( 5, 3, 1, 1 );

		bool aux = ( in8 ( *kbc, 0x64 ) & 0x21 ) == 0x21 && board->rises [ 12 ] >= 1;
		uint8_t packet [ 4 ] = { in8 ( *kbc, 0x60 ), in8 ( *kbc, 0x60 ), in8 ( *kbc, 0x60 ), in8 ( *kbc, 0x60 ) };

		expect ( mouse_reset && wheel_id && aux && packet [ 0 ] == 0x29 && packet [ 1 ] == 5 && packet [ 2 ] == 0xFD && packet [ 3 ] == 0x0F, "chipset: the PS/2 mouse resets, switches to wheel mode and sends 4-byte packets on IRQ 12" );

		out8 ( *kbc, 0x64, 0x60 );
		out8 ( *kbc, 0x60, 0x07 );
		kbc->key ( 0x9E );

		bool untranslated = in8 ( *kbc, 0x60 ) == 0xF0 && in8 ( *kbc, 0x60 ) == 0x1C;

		out8 ( *kbc, 0x64, 0xFE );

		expect ( untranslated && board->reset_seen, "chipset: without translation keys arrive in set 2, and command FE resets the machine" );

		delete kbc;
		delete board;

	}

	static void power_tests ( ) {

		fake_board* board = new fake_board ( );
		pc::power_management* power = new pc::power_management ( *board );
		pc::serial_port* serial = new pc::serial_port ( *board, 0x3F8, 4 );

		power->reset ( );

		uint32_t before = static_cast < uint32_t > ( power->read ( 0x608, 4 ) );

		board->time += 1000000000ull;

		uint32_t after = static_cast < uint32_t > ( power->read ( 0x608, 4 ) );

		power->write ( 0x602, 2, 0x0100 );
		power->press_power ( );

		bool button = board->rises [ 9 ] == 1 && ( power->read ( 0x600, 2 ) & 0x0100 );

		power->write ( 0x600, 2, 0x0100 );

		bool cleared = !( board->lines & ( 1 << 9 ) );

		power->write ( 0x604, 2, ( 5u << 10 ) | 0x2000 | 1 );

		expect ( after - before == 3579545 && button && cleared && board->off_seen && ( power->read ( 0x604, 2 ) & 1 ), "chipset: the ACPI PM timer runs at 3.58 MHz, the power button raises SCI and S5 powers off" );

		serial->reset ( );
		serial->write ( 0x3F8, 1, 'O' );
		serial->write ( 0x3F8, 1, 'K' );
		serial->write ( 0x3FC, 1, 0x08 );
		serial->write ( 0x3F9, 1, 0x02 );

		bool interrupt = board->rises [ 4 ] >= 1 && ( serial->read ( 0x3FA, 1 ) & 0x0F ) == 0x02 && !( board->lines & ( 1 << 4 ) );

		expect ( board->length == 2 && board->text [ 0 ] == 'O' && board->text [ 1 ] == 'K' && serial->read ( 0x3FD, 1 ) == 0x60 && interrupt, "chipset: the 16550 UART prints to the console and raises a transmit interrupt" );

		delete power;
		delete serial;
		delete board;

	}

	static void pci_tests ( ) {

		pc::pci_bus* bus = new pc::pci_bus ( );
		pc::pci_function* host = new pc::pci_function ( );
		pc::pci_function* card = new pc::pci_function ( );

		host->identify ( 0x8086, 0x1237, 0x06, 0x00, 0x00, 0x02, 0x00 );
		card->identify ( 0x1234, 0x5678, 0x03, 0x00, 0x00, 0x01, 0x00 );
		card->set_bar ( 0, 0x1000, false, 0 );
		bus->attach ( 0, 0, host );
		bus->attach ( 3, 0, card );

		auto config = [ & ] ( uint32_t device, uint32_t offset ) {

			bus->write ( 0xCF8, 4, 0x80000000u | device << 11 | offset );

			return static_cast < uint32_t > ( bus->read ( 0xCFC, 4 ) );

		};

		auto store = [ & ] ( uint32_t device, uint32_t offset, uint32_t value ) {

			bus->write ( 0xCF8, 4, 0x80000000u | device << 11 | offset );
			bus->write ( 0xCFC, 4, value );

		};

		bool found = config ( 0, 0 ) == 0x12378086 && config ( 5, 0 ) == 0xFFFFFFFF;

		store ( 3, 0x10, 0xFFFFFFFF );

		bool sized = config ( 3, 0x10 ) == 0xFFFFF000;

		store ( 3, 0x10, 0xE0000000 );
		store ( 3, 0x04, 0x0007 );
		store ( 3, 0x00, 0 );

		expect ( found && sized && config ( 3, 0x10 ) == 0xE0000000 && ( config ( 3, 0x04 ) & 0xFFFF ) == 0x0007 && config ( 3, 0 ) == 0x56781234, "chipset: PCI configuration finds functions, sizes BARs and keeps IDs read-only" );

		delete bus;
		delete host;
		delete card;

	}

	static const char* const ticking_program =
		"org 0x1000\n"
		"cli\n"
		"xor ax, ax\n"
		"mov ds, ax\n"
		"mov ss, ax\n"
		"mov sp, 0x0FF0\n"
		"mov word [0x20], tick\n"
		"mov word [0x22], 0\n"
		"mov al, 0x11\n"
		"out 0x20, al\n"
		"out 0xA0, al\n"
		"mov al, 0x08\n"
		"out 0x21, al\n"
		"mov al, 0x70\n"
		"out 0xA1, al\n"
		"mov al, 0x04\n"
		"out 0x21, al\n"
		"mov al, 0x02\n"
		"out 0xA1, al\n"
		"mov al, 0x01\n"
		"out 0x21, al\n"
		"out 0xA1, al\n"
		"mov al, 0xFE\n"
		"out 0x21, al\n"
		"mov al, 0xFF\n"
		"out 0xA1, al\n"
		"mov al, 0x34\n"
		"out 0x43, al\n"
		"mov al, 0xA9\n"
		"out 0x40, al\n"
		"mov al, 0x04\n"
		"out 0x40, al\n"
		"mov dx, 0x3F8\n"
		"mov al, 'O'\n"
		"out dx, al\n"
		"mov al, 'K'\n"
		"out dx, al\n"
		"sti\n"
		"idle: hlt\n"
		"cmp word [0x500], 40\n"
		"jb idle\n"
		"mov al, 'T'\n"
		"out 0xE9, al\n"
		"cli\n"
		"park: hlt\n"
		"jmp park\n"
		"tick: inc word [0x500]\n"
		"push ax\n"
		"mov al, 0x20\n"
		"out 0x20, al\n"
		"pop ax\n"
		"iret\n";

	static void platform_tests ( ) {

		if ( !hv::present ( ) ) {

			print ( "  skip  chipset: the Windows Hypervisor Platform isn't available on this PC\r\n" );

			return;

		}

		pc::platform* board = new pc::platform ( );
		assembler::result* program = new assembler::result ( );
		pc::platform_options options;
		char text [ 128 ] = { };
		bool ready = assembler::assemble ( ticking_program, base::text_length ( ticking_program ), nullptr, 0, *program );

		options.memory = 4ull * 1024 * 1024;
		ready = ready && board->create ( options );

		if ( ready ) {

			hv::cpu_state cpu;

			board->load ( 0x1000, program->bytes.data ( ), static_cast < uint32_t > ( program->bytes.count ( ) ) );
			ready = board->vm.get_state ( 0, cpu );
			cpu.segments [ hv::SEG_CS ].base = 0;
			cpu.segments [ hv::SEG_CS ].selector = 0;
			cpu.rip = 0x1000;
			cpu.rflags = 2;
			ready = ready && board->vm.set_state ( 0, cpu ) && board->start ( );

		}

		bool done = false;

		for ( int32_t waited = 0; ready && waited < 3000 && !done; waited += 5 ) {

			Sleep ( 5 );
			board->console_text ( text, sizeof ( text ) );
			done = text [ 0 ] == 'O' && text [ 1 ] == 'K' && text [ 2 ] == 'T';

		}

		uint32_t ticks = ready ? static_cast < uint32_t > ( board->vm.memory ( ) [ 0x500 ] | board->vm.memory ( ) [ 0x501 ] << 8 ) : 0;

		if ( ready )
			board->stop ( );

		if ( board->vm.problem [ 0 ] ) {

			char line [ 260 ] = "        ";

			append ( line, 260, board->vm.problem );
			append ( line, 260, "\r\n" );
			print ( line );

		}

		expect ( done && ticks >= 40, "chipset: real-mode code programs the PIC and PIT on the hypervisor, takes timer interrupts and prints through the UART" );

		delete program;
		delete board;

	}

	void chipset_tests ( ) {

		print ( "\r\n" );
		pic_tests ( );
		timer_tests ( );
		clock_tests ( );
		keyboard_tests ( );
		power_tests ( );
		pci_tests ( );
		platform_tests ( );

	}

}

#endif