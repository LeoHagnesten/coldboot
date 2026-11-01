#ifdef COLDBOOT_DEBUG

#include "assembler/assembler.hh"
#include "hv/machine.hh"
#include "pc/ioapic.hh"
#include "tests/harness.hh"

namespace tests {

	static const char* const guest_program =
		"org 0x1000\n"
		"mov al, 0x42\n"
		"out 0xE9, al\n"
		"mov dx, 0x3F8\n"
		"mov ax, 0x5A55\n"
		"out dx, ax\n"
		"mov word [0x2000], 0x1234\n"
		"hlt\n";

	static const uint8_t protected_program [ ] = {

		0xB8, 0x44, 0x33, 0x22, 0x11,
		0xA3, 0x00, 0x00, 0x00, 0xD0,
		0x8B, 0x1D, 0x04, 0x00, 0x00, 0xD0,
		0x83, 0x0D, 0x08, 0x00, 0x00, 0xD0, 0x40,
		0x89, 0x1D, 0x00, 0x30, 0x00, 0x00,
		0xBE, 0x00, 0x40, 0x00, 0x00,
		0xB9, 0x04, 0x00, 0x00, 0x00,
		0x66, 0xBA, 0xE9, 0x00,
		0xF3, 0x6E,
		0x0F, 0xB7, 0x05, 0x0C, 0x00, 0x00, 0xD0,
		0xA3, 0x04, 0x30, 0x00, 0x00,
		0xB0, 0xFF, 0xE6, 0xE9,
		0xF4,

	};

	static const uint8_t long_program [ ] = {

		0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
		0x48, 0xA3, 0x10, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00, 0x00,
		0xBF, 0x00, 0x00, 0x00, 0xD0,
		0x48, 0x8B, 0x57, 0x18,
		0x48, 0x89, 0x14, 0x25, 0x00, 0x30, 0x00, 0x00,
		0x41, 0xB8, 0x07, 0x00, 0x00, 0x00,
		0x44, 0x88, 0x47, 0x20,
		0xB0, 0xFF, 0xE6, 0xE9,
		0xF4,

	};

	static const uint8_t interrupt_program [ ] = {

		0xC7, 0x05, 0xF0, 0x00, 0xE0, 0xFE, 0xFF, 0x01, 0x00, 0x00,
		0xFB,
		0xF4,
		0xB0, 0x99, 0xE6, 0xE9,
		0xF4,

	};

	static const uint8_t interrupt_handler [ ] = { 0xB0, 0x77, 0xE6, 0xE9, 0xCF };

	static const uint8_t starter_program [ ] = {

		0xC7, 0x05, 0xF0, 0x00, 0xE0, 0xFE, 0xFF, 0x01, 0x00, 0x00,
		0xC7, 0x05, 0x10, 0x03, 0xE0, 0xFE, 0x00, 0x00, 0x00, 0x01,
		0xC7, 0x05, 0x00, 0x03, 0xE0, 0xFE, 0x00, 0x45, 0x00, 0x00,
		0xC7, 0x05, 0x00, 0x03, 0xE0, 0xFE, 0x08, 0x46, 0x00, 0x00,
		0xB0, 0xB0, 0xE6, 0xE9,
		0xF4,

	};

	static const uint8_t started_program [ ] = { 0xB0, 0xA1, 0xE6, 0xE9, 0xF4 };

	static const uint8_t identity_program [ ] = {

		0xB8, 0x00, 0x00, 0x00, 0x40,
		0x0F, 0xA2,
		0x89, 0x1D, 0x00, 0x30, 0x00, 0x00,
		0x89, 0x0D, 0x04, 0x30, 0x00, 0x00,
		0x89, 0x15, 0x08, 0x30, 0x00, 0x00,
		0xB8, 0x01, 0x00, 0x00, 0x00,
		0x0F, 0xA2,
		0x89, 0x0D, 0x0C, 0x30, 0x00, 0x00,
		0xB9, 0xFF, 0x02, 0x00, 0x00,
		0xB8, 0x06, 0x08, 0x00, 0x00,
		0x31, 0xD2,
		0x0F, 0x30,
		0x31, 0xC0,
		0x0F, 0x32,
		0xA3, 0x10, 0x30, 0x00, 0x00,
		0xB9, 0x34, 0x12, 0x00, 0x00,
		0x0F, 0x32,
		0xB0, 0xFF, 0xE6, 0xE9,
		0xF4,

	};

	static const uint8_t protection_handler [ ] = { 0x83, 0xC4, 0x04, 0x83, 0x04, 0x24, 0x02, 0xB0, 0x0D, 0xE6, 0xE9, 0xCF };

	static const uint8_t router_program [ ] = {

		0xC7, 0x05, 0xF0, 0x00, 0xE0, 0xFE, 0xFF, 0x01, 0x00, 0x00,
		0xC7, 0x05, 0x00, 0x00, 0xC0, 0xFE, 0x1A, 0x00, 0x00, 0x00,
		0xC7, 0x05, 0x10, 0x00, 0xC0, 0xFE, 0x45, 0x00, 0x00, 0x00,
		0xC7, 0x05, 0x00, 0x00, 0xC0, 0xFE, 0x1B, 0x00, 0x00, 0x00,
		0xC7, 0x05, 0x10, 0x00, 0xC0, 0xFE, 0x00, 0x00, 0x00, 0x00,
		0xC7, 0x05, 0x00, 0x00, 0xC0, 0xFE, 0x1C, 0x00, 0x00, 0x00,
		0xC7, 0x05, 0x10, 0x00, 0xC0, 0xFE, 0x46, 0x80, 0x00, 0x00,
		0xC7, 0x05, 0x00, 0x00, 0xC0, 0xFE, 0x1D, 0x00, 0x00, 0x00,
		0xC7, 0x05, 0x10, 0x00, 0xC0, 0xFE, 0x00, 0x00, 0x00, 0x00,
		0xB0, 0x01, 0xE6, 0xE9,
		0xFB,
		0xF4,
		0xEB, 0xFD,

	};

	static const uint8_t edge_handler [ ] = { 0xB0, 0x45, 0xE6, 0xE9, 0xC7, 0x05, 0xB0, 0x00, 0xE0, 0xFE, 0x00, 0x00, 0x00, 0x00, 0xCF };

	static const uint8_t level_handler [ ] = { 0xB0, 0x46, 0xE6, 0xE9, 0xB0, 0x00, 0xE6, 0xF0, 0xC7, 0x05, 0xB0, 0x00, 0xE0, 0xFE, 0x00, 0x00, 0x00, 0x00, 0xCF };

	static const uint8_t window_program [ ] = {

		0xC7, 0x05, 0xF0, 0x00, 0xE0, 0xFE, 0xFF, 0x01, 0x00, 0x00,
		0xC7, 0x05, 0x50, 0x03, 0xE0, 0xFE, 0x00, 0x07, 0x00, 0x00,
		0xFA,
		0xB0, 0x01, 0xE6, 0xE9,
		0xB9, 0x00, 0x00, 0x00, 0x02,
		0xE2, 0xFE,
		0xB0, 0x02, 0xE6, 0xE9,
		0xFB,
		0xF4,
		0xB0, 0x03, 0xE6, 0xE9,
		0xF4,

	};

	static const uint8_t window_handler [ ] = { 0xB0, 0x30, 0xE6, 0xE9, 0xCF };

	static const uint8_t counting_program [ ] = { 0xFF, 0x05, 0x00, 0x30, 0x00, 0x00, 0xEB, 0xF8 };

	static const uint8_t crashing_program [ ] = { 0xCD, 0x30, 0xF4 };

	struct port_log : hv::device {

		volatile long count = 0;
		uint32_t values [ 32 ] = { };

		uint64_t read ( uint64_t, uint32_t ) override {

			return 0xFF;

		}

		void write ( uint64_t, uint32_t, uint64_t value ) override {

			long slot = count;

			if ( slot < 32 ) {

				values [ slot ] = static_cast < uint32_t > ( value );
				InterlockedExchange ( &count, slot + 1 );

			}

		}

	};

	struct register_file : hv::device {

		uint8_t bytes [ 64 ] = { };

		uint64_t read ( uint64_t address, uint32_t size ) override {

			uint64_t value = 0;
			uint64_t at = address - 0xD0000000;

			for ( int32_t i = static_cast < int32_t > ( size ) - 1; i >= 0; i-- )
				value = value << 8 | ( at + i < sizeof ( bytes ) ? bytes [ at + i ] : 0xFF );

			return value;

		}

		void write ( uint64_t address, uint32_t size, uint64_t value ) override {

			uint64_t at = address - 0xD0000000;

			for ( uint32_t i = 0; i < size; i++ ) {

				if ( at + i < sizeof ( bytes ) )
					bytes [ at + i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

			}

		}

		uint64_t get ( uint32_t at, uint32_t size ) {

			return read ( 0xD0000000 + at, size );

		}

	};

	static hv::cpu_state flat_state ( bool wide ) {

		hv::cpu_state cpu;

		cpu.cr0 = 0x11;

		for ( hv::segment& part : cpu.segments ) {

			part.limit = 0xFFFFFFFF;
			part.selector = 0x10;
			part.attributes = 0xC093;

		}

		cpu.segments [ hv::SEG_CS ].selector = 0x08;
		cpu.segments [ hv::SEG_CS ].attributes = wide ? 0xA09B : 0xC09B;
		cpu.gpr [ hv::REG_SP ] = 0x9000;
		cpu.rip = 0x1000;

		if ( wide ) {

			cpu.cr0 = 0x80000011;
			cpu.cr3 = 0x10000;
			cpu.cr4 = 0x20;
			cpu.efer = 0x500;

		}

		return cpu;

	}

	static void place ( hv::machine& vm, uint64_t at, const uint8_t* bytes, uint32_t size ) {

		for ( uint32_t i = 0; i < size; i++ )
			vm.memory ( ) [ at + i ] = bytes [ i ];

	}

	static void place64 ( hv::machine& vm, uint64_t at, uint64_t value ) {

		for ( uint32_t i = 0; i < 8; i++ )
			vm.memory ( ) [ at + i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

	}

	static uint64_t peek ( hv::machine& vm, uint64_t at, uint32_t size ) {

		uint64_t value = 0;

		for ( int32_t i = static_cast < int32_t > ( size ) - 1; i >= 0; i-- )
			value = value << 8 | vm.memory ( ) [ at + i ];

		return value;

	}

	struct line_dropper : hv::device {

		pc::ioapic* router = nullptr;

		uint64_t read ( uint64_t, uint32_t ) override {

			return 0xFF;

		}

		void write ( uint64_t, uint32_t, uint64_t ) override {

			router->set_line ( 6, false );

		}

	};

	static void descriptors ( hv::machine& vm ) {

		place64 ( vm, 0x7008, 0x00CF9B000000FFFFull );
		place64 ( vm, 0x7010, 0x00CF93000000FFFFull );

	}

	static void gate ( hv::machine& vm, uint32_t vector, uint32_t handler ) {

		uint8_t entry [ 8 ] = { static_cast < uint8_t > ( handler ), static_cast < uint8_t > ( handler >> 8 ), 0x08, 0x00, 0x00, 0x8E, static_cast < uint8_t > ( handler >> 16 ), static_cast < uint8_t > ( handler >> 24 ) };

		place ( vm, 0x5000 + vector * 8, entry, 8 );

	}

	static bool wait_count ( hv::machine& vm, const port_log& log, long wanted ) {

		for ( uint32_t waited = 0; waited < 3000 && log.count < wanted && vm.running ( ); waited++ )
			Sleep ( 1 );

		return log.count >= wanted;

	}

	static bool settle ( hv::machine& vm, const port_log& log, long wanted ) {

		wait_count ( vm, log, wanted );
		vm.stop ( );

		if ( vm.reason == hv::STOP_FAILED ) {

			char line [ 260 ] = "        ";

			append ( line, 260, vm.problem );
			append ( line, 260, "\r\n" );
			print ( line );

		}

		return log.count >= wanted;

	}

	static void partition_tests ( ) {

		hv::partition* machine = new hv::partition ( );
		assembler::result* program = new assembler::result ( );
		bool created = machine->create ( 1, 2 * 1024 * 1024, 0 );
		bool assembled = assembler::assemble ( guest_program, base::text_length ( guest_program ), nullptr, 0, *program );

		if ( !created ) {

			char line [ 220 ] = "        ";

			append ( line, 220, machine->problem );
			append ( line, 220, "\r\n" );
			print ( line );

		}

		expect ( created && assembled, "hypervisor: a partition with one processor and 2 MB of memory starts" );

		uint32_t ports [ 4 ] = { };
		uint32_t values [ 4 ] = { };
		uint32_t sizes [ 4 ] = { };
		int32_t writes = 0;
		bool halted = false;

		if ( created && assembled ) {

			for ( int32_t i = 0; i < program->bytes.count ( ); i++ )
				machine->memory [ 0x1000 + i ] = program->bytes [ i ];

			WHV_REGISTER_NAME names [ 3 ] = { WHvX64RegisterCs, WHvX64RegisterRip, WHvX64RegisterRflags };
			WHV_REGISTER_VALUE state [ 3 ] = { };

			machine->registers ( 0, names, 3, state );
			state [ 0 ].Segment.Base = 0;
			state [ 0 ].Segment.Selector = 0;
			state [ 1 ].Reg64 = 0x1000;
			state [ 2 ].Reg64 = 0x2;
			machine->set_registers ( 0, names, 3, state );

			for ( int32_t step = 0; step < 16 && !halted; step++ ) {

				WHV_RUN_VP_EXIT_CONTEXT exit = { };

				if ( !machine->run ( 0, exit ) )
					break;

				if ( exit.ExitReason == WHvRunVpExitReasonX64IoPortAccess && exit.IoPortAccess.AccessInfo.IsWrite && writes < 4 ) {

					uint32_t size = exit.IoPortAccess.AccessInfo.AccessSize;

					ports [ writes ] = exit.IoPortAccess.PortNumber;
					sizes [ writes ] = size;
					values [ writes ] = static_cast < uint32_t > ( exit.IoPortAccess.Rax ) & ( size >= 4 ? 0xFFFFFFFFu : ( 1u << ( size * 8 ) ) - 1 );
					writes++;

					if ( !machine->skip ( 0, exit ) )
						break;

				} else if ( exit.ExitReason == WHvRunVpExitReasonX64Halt )
					halted = true;
				else
					break;

			}

		}

		expect ( writes == 2 && ports [ 0 ] == 0xE9 && sizes [ 0 ] == 1 && values [ 0 ] == 0x42 && ports [ 1 ] == 0x3F8 && sizes [ 1 ] == 2 && values [ 1 ] == 0x5A55, "hypervisor: real-mode code runs on the host CPU and its port writes exit to Coldboot" );
		expect ( halted && created && machine->memory [ 0x2000 ] == 0x34 && machine->memory [ 0x2001 ] == 0x12, "hypervisor: guest stores land in host memory and hlt stops the processor" );

		delete program;
		delete machine;

	}

	static void protected_tests ( ) {

		hv::machine* vm = new hv::machine ( );
		port_log* log = new port_log ( );
		register_file* file = new register_file ( );
		bool ready = vm->create ( 1, 2 * 1024 * 1024 );

		if ( ready ) {

			vm->attach_ports ( 0xE9, 0xE9, log );
			vm->attach_memory ( 0xD0000000, 0x1000, file );
			file->write ( 0xD0000004, 4, 0xCAFEBABE );
			file->write ( 0xD0000008, 4, 1 );
			file->write ( 0xD000000C, 2, 0x5A5A );
			place ( *vm, 0x1000, protected_program, sizeof ( protected_program ) );
			place ( *vm, 0x4000, reinterpret_cast < const uint8_t* > ( "ABCD" ), 4 );
			ready = vm->set_state ( 0, flat_state ( false ) ) && vm->start ( );

		}

		bool finished = ready && settle ( *vm, *log, 5 );

		expect ( finished && file->get ( 0, 4 ) == 0x11223344 && peek ( *vm, 0x3000, 4 ) == 0xCAFEBABE && file->get ( 8, 4 ) == 0x41, "hypervisor: 32-bit MMIO stores, loads and read-modify-write go through Coldboot's emulator" );
		expect ( finished && log->values [ 0 ] == 'A' && log->values [ 3 ] == 'D' && log->values [ 4 ] == 0xFF && peek ( *vm, 0x3004, 4 ) == 0x5A5A, "hypervisor: rep outsb and movzx from a device both complete in protected mode" );

		delete vm;
		delete log;
		delete file;

	}

	static void long_tests ( ) {

		hv::machine* vm = new hv::machine ( );
		port_log* log = new port_log ( );
		register_file* file = new register_file ( );
		bool ready = vm->create ( 1, 2 * 1024 * 1024 );

		if ( ready ) {

			vm->attach_ports ( 0xE9, 0xE9, log );
			vm->attach_memory ( 0xD0000000, 0x1000, file );
			file->write ( 0xD0000018, 8, 0xA1B2C3D4E5F60718ull );
			place64 ( *vm, 0x10000, 0x11003 );
			place64 ( *vm, 0x11000, 0x12003 );
			place64 ( *vm, 0x11000 + 3 * 8, 0x13003 );
			place64 ( *vm, 0x12000, 0x83 );
			place64 ( *vm, 0x13000 + 0x80 * 8, 0xD0000083ull );
			place ( *vm, 0x1000, long_program, sizeof ( long_program ) );
			ready = vm->set_state ( 0, flat_state ( true ) ) && vm->start ( );

		}

		bool finished = ready && settle ( *vm, *log, 1 );

		expect ( finished && file->get ( 0x10, 8 ) == 0x1122334455667788ull && peek ( *vm, 0x3000, 8 ) == 0xA1B2C3D4E5F60718ull && file->get ( 0x20, 1 ) == 7, "hypervisor: 64-bit code moves quadwords and REX byte registers to and from a device" );
		expect ( finished && ( peek ( *vm, 0x13000 + 0x80 * 8, 1 ) & 0x60 ) == 0x60, "hypervisor: the page walker marks the device page accessed and dirty in long mode" );

		delete vm;
		delete log;
		delete file;

	}

	static void interrupt_tests ( ) {

		hv::machine* vm = new hv::machine ( );
		port_log* log = new port_log ( );
		bool ready = vm->create ( 1, 2 * 1024 * 1024 );

		if ( ready ) {

			static const uint8_t gate [ 8 ] = { 0x00, 0x11, 0x08, 0x00, 0x00, 0x8E, 0x00, 0x00 };

			vm->attach_ports ( 0xE9, 0xE9, log );
			place64 ( *vm, 0x7008, 0x00CF9B000000FFFFull );
			place64 ( *vm, 0x7010, 0x00CF93000000FFFFull );
			place ( *vm, 0x5000 + 0x40 * 8, gate, 8 );
			place ( *vm, 0x1000, interrupt_program, sizeof ( interrupt_program ) );
			place ( *vm, 0x1100, interrupt_handler, sizeof ( interrupt_handler ) );
			ready = vm->set_state ( 0, flat_state ( false ) ) && vm->set_tables ( 0, 0x7000, 23, 0x5000, 0x7FF ) && vm->start ( );

		}

		if ( ready ) {

			Sleep ( 40 );
			ready = vm->interrupt ( 0, 0x40 );

		}

		bool finished = ready && settle ( *vm, *log, 2 );

		expect ( finished && log->values [ 0 ] == 0x77 && log->values [ 1 ] == 0x99, "hypervisor: an interrupt wakes a halted processor, runs its IDT handler and returns" );

		delete vm;
		delete log;

	}

	static void processor_tests ( ) {

		hv::machine* vm = new hv::machine ( );
		port_log* log = new port_log ( );
		bool ready = vm->create ( 2, 2 * 1024 * 1024 );

		if ( ready ) {

			vm->attach_ports ( 0xE9, 0xE9, log );
			place ( *vm, 0x1000, starter_program, sizeof ( starter_program ) );
			place ( *vm, 0x8000, started_program, sizeof ( started_program ) );
			ready = vm->set_state ( 0, flat_state ( false ) ) && vm->park ( 1 ) && vm->start ( );

		}

		bool finished = ready && settle ( *vm, *log, 2 );
		bool started = false;
		bool reported = false;

		for ( long i = 0; i < log->count && i < 32; i++ ) {

			started = started || log->values [ i ] == 0xA1;
			reported = reported || log->values [ i ] == 0xB0;

		}

		expect ( finished && started && reported, "hypervisor: processor 0 wakes processor 1 with INIT and SIPI through the local APIC" );

		delete vm;
		delete log;

	}

	static void identity_tests ( ) {

		hv::machine* vm = new hv::machine ( );
		port_log* log = new port_log ( );
		bool ready = vm->create ( 1, 2 * 1024 * 1024 );

		if ( ready ) {

			vm->attach_ports ( 0xE9, 0xE9, log );
			descriptors ( *vm );
			gate ( *vm, 13, 0x1100 );
			place ( *vm, 0x1000, identity_program, sizeof ( identity_program ) );
			place ( *vm, 0x1100, protection_handler, sizeof ( protection_handler ) );
			ready = vm->set_state ( 0, flat_state ( false ) ) && vm->set_tables ( 0, 0x7000, 23, 0x5000, 0x7FF ) && vm->start ( );

		}

		bool finished = ready && settle ( *vm, *log, 2 );
		bool named = finished && peek ( *vm, 0x3000, 4 ) == 0x646C6F43 && peek ( *vm, 0x3004, 4 ) == 0x746F6F62 && peek ( *vm, 0x3008, 4 ) == 0x20204D56;
		uint64_t features = peek ( *vm, 0x300C, 4 );

		expect ( named && ( features >> 31 & 1 ) && !( features >> 21 & 1 ), "hypervisor: CPUID names Coldboot as the hypervisor and hides x2APIC" );
		expect ( finished && peek ( *vm, 0x3010, 4 ) == 0x806 && log->values [ 0 ] == 0x0D && log->values [ 1 ] == 0xFF && vm->msr_exits > 0, "hypervisor: MTRR writes read back and an unknown MSR raises #GP through Coldboot" );

		delete vm;
		delete log;

	}

	static void router_tests ( ) {

		hv::machine* vm = new hv::machine ( );
		port_log* log = new port_log ( );
		pc::ioapic* router = new pc::ioapic ( *vm );
		line_dropper* dropper = new line_dropper ( );
		bool ready = vm->create ( 1, 2 * 1024 * 1024 );

		dropper->router = router;

		if ( ready ) {

			vm->attach_ports ( 0xE9, 0xE9, log );
			vm->attach_ports ( 0xF0, 0xF0, dropper );
			vm->attach_memory ( pc::ioapic::base, pc::ioapic::window, router );
			vm->listen_eoi ( router );
			descriptors ( *vm );
			gate ( *vm, 0x45, 0x1100 );
			gate ( *vm, 0x46, 0x1200 );
			place ( *vm, 0x1000, router_program, sizeof ( router_program ) );
			place ( *vm, 0x1100, edge_handler, sizeof ( edge_handler ) );
			place ( *vm, 0x1200, level_handler, sizeof ( level_handler ) );
			ready = vm->set_state ( 0, flat_state ( false ) ) && vm->set_tables ( 0, 0x7000, 23, 0x5000, 0x7FF ) && vm->start ( );

		}

		bool edge = false;
		bool level = false;
		uint64_t entry = 0;

		if ( ready && wait_count ( *vm, *log, 1 ) ) {

			router->set_line ( 5, true );
			edge = wait_count ( *vm, *log, 2 );
			router->set_line ( 5, false );
			router->set_line ( 6, true );
			level = wait_count ( *vm, *log, 3 );
			Sleep ( 50 );
			entry = router->entry ( 6 );

		}

		bool finished = ready && settle ( *vm, *log, 3 );

		expect ( finished && edge && log->values [ 0 ] == 0x01 && log->values [ 1 ] == 0x45, "hypervisor: the IOAPIC routes an edge-triggered line to its vector through the local APIC" );
		expect ( finished && level && log->values [ 2 ] == 0x46 && log->count == 3 && !( entry >> 14 & 1 ), "hypervisor: a level-triggered line holds remote IRR until the guest's EOI clears it" );

		delete vm;
		delete log;
		delete router;
		delete dropper;

	}

	static void window_tests ( ) {

		hv::machine* vm = new hv::machine ( );
		port_log* log = new port_log ( );
		bool ready = vm->create ( 1, 2 * 1024 * 1024 );

		if ( ready ) {

			vm->attach_ports ( 0xE9, 0xE9, log );
			descriptors ( *vm );
			gate ( *vm, 0x30, 0x1300 );
			place ( *vm, 0x1000, window_program, sizeof ( window_program ) );
			place ( *vm, 0x1300, window_handler, sizeof ( window_handler ) );
			ready = vm->set_state ( 0, flat_state ( false ) ) && vm->set_tables ( 0, 0x7000, 23, 0x5000, 0x7FF ) && vm->start ( );

		}

		if ( ready && wait_count ( *vm, *log, 1 ) )
			ready = vm->external ( 0, 0x30 );

		bool finished = ready && settle ( *vm, *log, 4 );

		expect ( finished && log->values [ 0 ] == 1 && log->values [ 1 ] == 2 && log->values [ 2 ] == 0x30 && log->values [ 3 ] == 3, "hypervisor: a PIC-style interrupt raised during cli waits for the interrupt window" );

		delete vm;
		delete log;

	}

	static void pause_tests ( ) {

		hv::machine* vm = new hv::machine ( );
		bool ready = vm->create ( 1, 2 * 1024 * 1024 );

		if ( ready ) {

			place ( *vm, 0x1000, counting_program, sizeof ( counting_program ) );
			ready = vm->set_state ( 0, flat_state ( false ) ) && vm->start ( );

		}

		bool paused = false;
		bool readable = false;
		uint64_t before = 0;
		uint64_t during = 0;
		uint64_t after = 0;

		if ( ready ) {

			Sleep ( 30 );
			paused = vm->pause ( );
			before = peek ( *vm, 0x3000, 4 );
			Sleep ( 30 );
			during = peek ( *vm, 0x3000, 4 );

			hv::cpu_state cpu;

			readable = vm->get_state ( 0, cpu ) && cpu.rip >= 0x1000 && cpu.rip < 0x1008;
			vm->resume ( );
			Sleep ( 30 );
			after = peek ( *vm, 0x3000, 4 );
			vm->stop ( );

		}

		expect ( paused && readable && before > 0 && before == during && after > during, "hypervisor: pause freezes every processor so state can be read, and resume carries on" );

		delete vm;

	}

	static void shutdown_tests ( ) {

		hv::machine* vm = new hv::machine ( );
		port_log* log = new port_log ( );
		bool ready = vm->create ( 1, 2 * 1024 * 1024 );

		if ( ready ) {

			place ( *vm, 0x1000, crashing_program, sizeof ( crashing_program ) );
			ready = vm->set_state ( 0, flat_state ( false ) ) && vm->set_tables ( 0, 0x7000, 23, 0x5000, 0 ) && vm->start ( );

		}

		if ( ready )
			settle ( *vm, *log, 1 );

		expect ( ready && vm->reason == hv::STOP_SHUTDOWN, "hypervisor: a triple fault stops the machine as a shutdown, ready for a reset" );

		delete vm;
		delete log;

	}

	void hypervisor_tests ( ) {

		print ( "\r\n" );

		if ( !hv::present ( ) ) {

			print ( "  skip  hypervisor: the Windows Hypervisor Platform isn't available on this PC\r\n" );

			return;

		}

		partition_tests ( );
		protected_tests ( );
		long_tests ( );
		interrupt_tests ( );
		processor_tests ( );
		identity_tests ( );
		router_tests ( );
		window_tests ( );
		pause_tests ( );
		shutdown_tests ( );

	}

}

#endif