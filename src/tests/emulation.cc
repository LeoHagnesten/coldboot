#ifdef COLDBOOT_DEBUG

#include "hv/emulator.hh"
#include "tests/harness.hh"

namespace tests {

	struct fake_bus : hv::guest_bus {

		uint8_t ram [ 0x10000 ] = { };
		uint32_t ports [ 8 ] = { };
		uint32_t port_count = 0;

		void read ( uint64_t address, uint8_t* out, uint32_t size ) override {

			for ( uint32_t i = 0; i < size; i++ )
				out [ i ] = address + i < sizeof ( ram ) ? ram [ address + i ] : 0xFF;

		}

		void write ( uint64_t address, const uint8_t* data, uint32_t size ) override {

			for ( uint32_t i = 0; i < size; i++ ) {

				if ( address + i < sizeof ( ram ) )
					ram [ address + i ] = data [ i ];

			}

		}

		uint32_t in ( uint16_t, uint32_t ) override {

			return 0xA55A;

		}

		void out ( uint16_t, uint32_t, uint32_t value ) override {

			if ( port_count < 8 )
				ports [ port_count++ ] = value;

		}

		void put32 ( uint32_t address, uint32_t value ) {

			for ( uint32_t i = 0; i < 4; i++ )
				ram [ address + i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

		}

		uint32_t get32 ( uint32_t address ) const {

			return static_cast < uint32_t > ( ram [ address ] ) | static_cast < uint32_t > ( ram [ address + 1 ] ) << 8 | static_cast < uint32_t > ( ram [ address + 2 ] ) << 16 | static_cast < uint32_t > ( ram [ address + 3 ] ) << 24;

		}

	};

	static hv::cpu_state real_mode ( ) {

		hv::cpu_state cpu;

		cpu.segments [ hv::SEG_CS ].attributes = 0x9B;

		return cpu;

	}

	static hv::cpu_state flat32 ( ) {

		hv::cpu_state cpu;

		cpu.cr0 = 0x11;

		for ( hv::segment& part : cpu.segments ) {

			part.limit = 0xFFFFFFFF;
			part.attributes = 0xC093;

		}

		cpu.segments [ hv::SEG_CS ].attributes = 0xC09B;

		return cpu;

	}

	static hv::cpu_state long64 ( fake_bus& bus ) {

		hv::cpu_state cpu = flat32 ( );

		cpu.cr0 = 0x80000011;
		cpu.cr3 = 0x8000;
		cpu.cr4 = 0x20;
		cpu.efer = 0x500;
		cpu.segments [ hv::SEG_CS ].attributes = 0xA09B;
		bus.put32 ( 0x8000, 0x9003 );
		bus.put32 ( 0x9000, 0xA003 );
		bus.put32 ( 0xA000, 0x0083 );

		return cpu;

	}

	void emulator_tests ( ) {

		print ( "\r\n" );

		fake_bus* bus = new fake_bus ( );
		hv::cpu_state cpu = real_mode ( );

		static const uint8_t store16 [ ] = { 0x89, 0x40, 0x04 };

		cpu.segments [ hv::SEG_DS ].base = 0x2000;
		cpu.gpr [ hv::REG_BX ] = 0x10;
		cpu.gpr [ hv::REG_SI ] = 0x20;
		cpu.gpr [ hv::REG_AX ] = 0xBEEF;
		cpu.rip = 0x100;

		bool done = hv::emulate ( cpu, *bus, store16, sizeof ( store16 ) ) == hv::EMULATE_DONE;

		expect ( done && bus->ram [ 0x2034 ] == 0xEF && bus->ram [ 0x2035 ] == 0xBE && cpu.rip == 0x103, "emulator: 16-bit mov [bx+si+4], ax uses the DS base and steps IP" );

		cpu = flat32 ( );

		static const uint8_t add_sib [ ] = { 0x83, 0x04, 0x8D, 0x00, 0x01, 0x00, 0x00, 0x05 };

		bus->put32 ( 0x108, 0xFFFFFFFE );
		cpu.gpr [ hv::REG_CX ] = 2;
		done = hv::emulate ( cpu, *bus, add_sib, sizeof ( add_sib ) ) == hv::EMULATE_DONE;

		expect ( done && bus->get32 ( 0x108 ) == 3 && ( cpu.rflags & hv::FLAG_ARITHMETIC ) == ( hv::FLAG_CF | hv::FLAG_AF | hv::FLAG_PF ), "emulator: add dword [ecx*4+0x100], 5 wraps with carry, adjust and parity" );

		cpu = long64 ( *bus );

		static const uint8_t relative [ ] = { 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00 };

		bus->put32 ( 0x1016, 0x12345678 );
		cpu.rip = 0x1000;
		cpu.gpr [ hv::REG_AX ] = ~0ull;
		done = hv::emulate ( cpu, *bus, relative, sizeof ( relative ) ) == hv::EMULATE_DONE;

		expect ( done && cpu.gpr [ hv::REG_AX ] == 0x12345678 && cpu.rip == 0x1006 && ( bus->ram [ 0x8000 ] & 0x20 ) && ( bus->ram [ 0xA000 ] & 0x20 ), "emulator: 64-bit RIP-relative load walks four levels, zero-extends and marks entries accessed" );

		cpu = flat32 ( );

		static const uint8_t copy_down [ ] = { 0xF3, 0xA5 };

		for ( uint32_t i = 0; i < 16; i++ )
			bus->ram [ 0x2000 + i ] = static_cast < uint8_t > ( i + 1 );

		cpu.gpr [ hv::REG_SI ] = 0x200C;
		cpu.gpr [ hv::REG_DI ] = 0x300C;
		cpu.gpr [ hv::REG_CX ] = 4;
		cpu.rflags |= hv::FLAG_DF;
		done = hv::emulate ( cpu, *bus, copy_down, sizeof ( copy_down ) ) == hv::EMULATE_DONE;

		bool copied = true;

		for ( uint32_t i = 0; i < 16; i++ )
			copied = copied && bus->ram [ 0x3000 + i ] == i + 1;

		expect ( done && copied && cpu.gpr [ hv::REG_SI ] == 0x1FFC && cpu.gpr [ hv::REG_DI ] == 0x2FFC && cpu.gpr [ hv::REG_CX ] == 0 && cpu.rip == 2, "emulator: rep movsd with the direction flag set copies downwards" );

		cpu = long64 ( *bus );

		static const uint8_t store_unmapped [ ] = { 0x88, 0x03 };

		cpu.rip = 0x1000;
		cpu.gpr [ hv::REG_BX ] = 0x40000000;

		hv::emulate_result result = hv::emulate ( cpu, *bus, store_unmapped, sizeof ( store_unmapped ) );

		expect ( result == hv::EMULATE_FAULT && cpu.fault_address == 0x40000000 && cpu.fault_code == 2 && cpu.rip == 0x1000, "emulator: a store to an unmapped page reports a write page fault and leaves RIP alone" );

		cpu = flat32 ( );

		static const uint8_t extend [ ] = { 0x0F, 0xBE, 0x05, 0x00, 0x05, 0x00, 0x00 };

		bus->ram [ 0x500 ] = 0x80;
		done = hv::emulate ( cpu, *bus, extend, sizeof ( extend ) ) == hv::EMULATE_DONE;

		expect ( done && cpu.gpr [ hv::REG_AX ] == 0xFFFFFF80, "emulator: movsx eax, byte [mem] sign-extends" );

		cpu = real_mode ( );

		static const uint8_t swap_high [ ] = { 0x86, 0x26, 0x00, 0x06 };

		bus->ram [ 0x600 ] = 0x99;
		cpu.gpr [ hv::REG_AX ] = 0x1234;
		done = hv::emulate ( cpu, *bus, swap_high, sizeof ( swap_high ) ) == hv::EMULATE_DONE;

		expect ( done && bus->ram [ 0x600 ] == 0x12 && cpu.gpr [ hv::REG_AX ] == 0x9934, "emulator: xchg [mem], ah reaches the high byte register" );

		cpu = flat32 ( );

		static const uint8_t send [ ] = { 0xF3, 0x6E };
		static const uint8_t receive [ ] = { 0xF3, 0x66, 0x6D };

		bus->ram [ 0x700 ] = 'x';
		bus->ram [ 0x701 ] = 'y';
		bus->ram [ 0x702 ] = 'z';
		cpu.gpr [ hv::REG_SI ] = 0x700;
		cpu.gpr [ hv::REG_CX ] = 3;
		cpu.gpr [ hv::REG_DX ] = 0xE9;
		done = hv::emulate ( cpu, *bus, send, sizeof ( send ) ) == hv::EMULATE_DONE;
		cpu.gpr [ hv::REG_DI ] = 0x800;
		cpu.gpr [ hv::REG_CX ] = 2;
		done = done && hv::emulate ( cpu, *bus, receive, sizeof ( receive ) ) == hv::EMULATE_DONE;

		static const uint8_t single [ ] = { 0xE6, 0xE9 };

		cpu.gpr [ hv::REG_AX ] = 0x5AFF;
		done = done && hv::emulate ( cpu, *bus, single, sizeof ( single ) ) == hv::EMULATE_DONE;

		expect ( done && bus->port_count == 4 && bus->ports [ 0 ] == 'x' && bus->ports [ 2 ] == 'z' && bus->ports [ 3 ] == 0xFF && bus->get32 ( 0x800 ) == 0xA55AA55A && cpu.gpr [ hv::REG_DI ] == 0x804, "emulator: string and single port writes carry only their own bytes" );

		cpu = flat32 ( );
		cpu.rip = 0x40;

		static const uint8_t system_call [ ] = { 0x0F, 0x05 };

		result = hv::emulate ( cpu, *bus, system_call, sizeof ( system_call ) );

		expect ( result == hv::EMULATE_UNSUPPORTED && cpu.rip == 0x40, "emulator: instructions it doesn't handle are refused without side effects" );

		cpu = flat32 ( );
		cpu.cr0 = 0x80000011;
		cpu.cr3 = 0xC000;
		bus->put32 ( 0xC000, 0xD003 );
		bus->put32 ( 0xD000 + 5 * 4, 0x6003 );
		cpu.gpr [ hv::REG_AX ] = 0xAABBCCDD;

		static const uint8_t paged_store [ ] = { 0xA3, 0x10, 0x50, 0x00, 0x00 };

		done = hv::emulate ( cpu, *bus, paged_store, sizeof ( paged_store ) ) == hv::EMULATE_DONE;

		expect ( done && bus->get32 ( 0x6010 ) == 0xAABBCCDD && bus->ram [ 0xD014 ] == 0x63, "emulator: 32-bit paging maps 0x5010 to 0x6010 and marks the page dirty" );

		cpu = flat32 ( );
		cpu.cr0 = 0x80000011;
		cpu.cr3 = 0xE000;
		cpu.cr4 = 0x20;
		bus->put32 ( 0xE000, 0xF001 );
		bus->put32 ( 0xF000, 0x0083 );
		cpu.gpr [ hv::REG_AX ] = 0x5C;

		static const uint8_t pae_store [ ] = { 0xA2, 0x40, 0x00, 0x00, 0x00 };

		done = hv::emulate ( cpu, *bus, pae_store, sizeof ( pae_store ) ) == hv::EMULATE_DONE;

		expect ( done && bus->ram [ 0x40 ] == 0x5C && bus->ram [ 0xF000 ] == 0xE3, "emulator: PAE paging with a 2 MB page stores through and marks it dirty" );

		delete bus;

	}

}

#endif