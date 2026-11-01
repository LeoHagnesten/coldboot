#ifdef COLDBOOT_DEBUG

#include "assembler/assembler.hh"
#include "hv/platform.hh"
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

	void hypervisor_tests ( ) {

		print ( "\r\n" );

		if ( !hv::present ( ) ) {

			print ( "  skip  hypervisor: the Windows Hypervisor Platform isn't available on this PC\r\n" );

			return;

		}

		hv::partition* machine = new hv::partition ( );
		assembler::result* program = new assembler::result ( );
		bool created = machine->create ( 1, 2 * 1024 * 1024 );
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

}

#endif