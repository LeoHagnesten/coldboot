#ifdef COLDBOOT_DEBUG

#include "cpu/cpu.hh"
#include "tests/harness.hh"

namespace tests {

	struct echo_port : machine::port_device {

		uint8_t latch = 0;

		uint8_t in ( uint16_t ) override {

			return latch;

		}

		void out ( uint16_t, uint8_t value ) override {

			latch = value;

		}

	};

	struct test_line : machine::interrupt_line {

		bool raised = false;

		bool pending ( ) override {

			return raised;

		}

		uint8_t acknowledge ( ) override {

			raised = false;

			return 0x08;

		}

	};

	static void load_program ( machine::cpu& computer, const uint8_t* code, int32_t size ) {

		computer.reset ( );
		computer.system.clear ( );
		computer.state.segments [ machine::SEG_CS ] = 0x0100;
		computer.state.segments [ machine::SEG_SS ] = 0x0200;
		computer.state.segments [ machine::SEG_DS ] = 0x0400;
		computer.state.segments [ machine::SEG_ES ] = 0x0400;
		computer.state.general [ machine::REG_SP ] = 0x0100;
		computer.state.general [ machine::REG_AX ] = 0x0200;
		computer.state.ip = 0;

		for ( int32_t i = 0; i < size; i++ )
			computer.system.write8 ( 0x1000 + static_cast < uint32_t > ( i ), code [ i ] );

		for ( uint32_t vector = 0; vector < 256; vector++ ) {

			computer.system.write16 ( vector * 4, static_cast < uint16_t > ( vector ) );
			computer.system.write16 ( vector * 4 + 2, 0x0300 );
			computer.system.write8 ( 0x3000 + vector, 0xF4 );

		}

	}

	static bool vectored_to ( machine::cpu& computer, uint8_t vector, uint16_t return_offset ) {

		machine::registers& r = computer.state;
		uint16_t pushed = computer.system.read16 ( machine::cpu::linear ( r.segments [ machine::SEG_SS ], r.general [ machine::REG_SP ] ) );

		return r.segments [ machine::SEG_CS ] == 0x0300 && r.ip == vector && pushed == return_offset && !( r.flags & ( machine::FLAG_INTERRUPT | machine::FLAG_TRAP ) );

	}

	static void interrupt_tests ( machine::cpu& computer ) {

		test_line line;
		machine::registers& r = computer.state;

		{

			static const uint8_t code [ ] = { 0xFB, 0x90, 0x90 };

			load_program ( computer, code, sizeof ( code ) );
			computer.controller = &line;
			line.raised = true;
			computer.step ( );
			expect ( r.segments [ machine::SEG_CS ] == 0x0100 && r.ip == 1, "INTR waits one instruction after STI" );
			computer.step ( );
			expect ( vectored_to ( computer, 0x08, 2 ), "INTR vectors through the controller and pushes the return address" );

		}

		{

			static const uint8_t code [ ] = { 0x90, 0x90 };

			load_program ( computer, code, sizeof ( code ) );
			computer.controller = &line;
			line.raised = true;
			computer.step ( );
			expect ( r.segments [ machine::SEG_CS ] == 0x0100 && r.ip == 1, "INTR is ignored while IF is clear" );
			line.raised = false;
			computer.nmi = true;
			computer.step ( );
			expect ( vectored_to ( computer, 0x02, 2 ) && !computer.nmi, "NMI is taken while IF is clear" );

		}

		{

			static const uint8_t code [ ] = { 0xFB, 0xF4, 0x90 };

			load_program ( computer, code, sizeof ( code ) );
			computer.controller = &line;
			computer.step ( );
			computer.step ( );

			uint64_t before = computer.cycles;

			computer.step ( );
			computer.step ( );
			expect ( computer.halted && r.ip == 2 && computer.cycles > before, "HLT waits for an interrupt while time passes" );
			line.raised = true;
			computer.step ( );
			expect ( vectored_to ( computer, 0x08, 2 ) && !computer.halted, "an interrupt wakes HLT and returns past it" );

		}

		{

			static const uint8_t code [ ] = { 0xFB, 0x8E, 0xD0, 0x90 };

			load_program ( computer, code, sizeof ( code ) );
			computer.controller = &line;
			computer.step ( );
			line.raised = true;
			computer.step ( );
			expect ( r.segments [ machine::SEG_CS ] == 0x0100 && r.ip == 3, "a segment register load holds interrupts for one instruction" );
			computer.step ( );
			expect ( vectored_to ( computer, 0x08, 4 ), "the held interrupt arrives after the next instruction" );

		}

		{

			static const uint8_t code [ ] = { 0x90, 0x90 };

			load_program ( computer, code, sizeof ( code ) );
			r.flags = static_cast < uint16_t > ( r.flags | machine::FLAG_TRAP );
			computer.step ( );
			expect ( vectored_to ( computer, 0x01, 1 ), "the trap flag raises vector 1 after each instruction" );

		}

		{

			static const uint8_t code [ ] = { 0x9D, 0x90, 0x90 };

			load_program ( computer, code, sizeof ( code ) );
			computer.system.write16 ( machine::cpu::linear ( 0x0200, 0x0100 ), 0xF102 );
			computer.step ( );
			expect ( r.segments [ machine::SEG_CS ] == 0x0100 && r.ip == 1 && ( r.flags & machine::FLAG_TRAP ), "a trap set by POPF waits for the next instruction" );
			computer.step ( );
			expect ( vectored_to ( computer, 0x01, 2 ), "the trap then fires after that instruction" );

		}

		{

			static const uint8_t code [ ] = { 0xFB, 0xF3, 0x2E, 0xA4, 0x90 };

			load_program ( computer, code, sizeof ( code ) );
			computer.controller = &line;
			r.general [ machine::REG_CX ] = 5;
			r.general [ machine::REG_DI ] = 0x10;
			computer.step ( );
			line.raised = true;
			computer.step ( );
			expect ( vectored_to ( computer, 0x08, 2 ) && r.general [ machine::REG_CX ] == 4, "an interrupted REP string resumes at its last prefix, as on the 8088" );

		}

		{

			static const uint8_t code [ ] = { 0xF6, 0xF1, 0x90 };

			load_program ( computer, code, sizeof ( code ) );
			r.general [ machine::REG_CX ] = 0;
			computer.step ( );
			expect ( vectored_to ( computer, 0x00, 2 ), "division by zero raises vector 0 past the instruction" );

		}

		{

			static const uint8_t code [ ] = { 0x90, 0xB8, 0x34, 0x12 };

			load_program ( computer, code, sizeof ( code ) );
			computer.queue = 4;
			computer.step ( );
			expect ( computer.cycles == 3, "NOP takes 3 clocks from a full queue" );
			computer.queue = 0;
			computer.step ( );
			expect ( computer.cycles == 15 && r.general [ machine::REG_AX ] == 0x1234, "MOV AX, imm16 takes 12 clocks from an empty queue" );

		}

		computer.controller = nullptr;

	}

	int32_t foundation_tests ( ) {

		machine::bus* memory = new machine::bus ( );
		machine::cpu* computer = new machine::cpu ( *memory );

		print ( "Coldboot foundation tests\r\n\r\n" );

		computer->reset ( );
		expect ( computer->state.segments [ machine::SEG_CS ] == 0xFFFF && computer->state.ip == 0x0000, "reset puts CS:IP at FFFF:0000" );
		expect ( computer->instruction_address ( ) == 0xFFFF0, "reset vector is linear FFFF0" );
		expect ( computer->state.flags == machine::FLAG_FIXED, "reset flags read as F002" );

		computer->system.write8 ( 0x00000, 0x00 );
		computer->system.write8 ( 0x100000, 0x5A );
		expect ( computer->system.read8 ( 0x00000 ) == 0x5A, "addresses wrap at 1 MB while A20 is off" );

		computer->system.a20 = true;
		computer->system.write8 ( 0x100000, 0xA5 );
		expect ( computer->system.read8 ( 0x100000 ) == 0xA5 && computer->system.read8 ( 0x00000 ) == 0x5A, "the high memory area is separate while A20 is on" );
		computer->system.a20 = false;

		uint8_t firmware [ 0x1000 ];

		for ( int32_t i = 0; i < 0x1000; i++ )
			firmware [ i ] = static_cast < uint8_t > ( i * 7 );

		expect ( computer->system.load_rom ( 0xFF000, firmware, 0x1000 ), "firmware loads at FF000" );
		computer->system.write8 ( 0xFFFF0, 0x00 );
		expect ( computer->system.read8 ( 0xFFFF0 ) == static_cast < uint8_t > ( 0xFF0 * 7 ), "writes to firmware are ignored" );

		computer->system.write16 ( 0x00400, 0xBEEF );
		expect ( computer->system.read16 ( 0x00400 ) == 0xBEEF, "16-bit reads and writes are little endian" );

		echo_port device;

		computer->system.attach ( 0x60, 0x64, &device );
		computer->system.out8 ( 0x61, 0x42 );
		expect ( computer->system.in8 ( 0x64 ) == 0x42, "ports reach the attached device" );
		expect ( computer->system.in8 ( 0x80 ) == 0xFF, "unclaimed ports read as FF" );

		print ( "\r\n" );
		interrupt_tests ( *computer );

		delete computer;
		delete memory;
		device_tests ( );
		storage_tests ( );
		assembler_tests ( );
		bios_tests ( );
		hypervisor_tests ( );
		print ( failure_count ( ) ? "\r\nSome tests failed.\r\n" : "\r\nAll tests passed.\r\n" );

		return failure_count ( );

	}

}

#endif