#include "hv/machine.hh"
#include "hv/paging.hh"

namespace hv {

	static const WHV_REGISTER_NAME state_names [ ] = {

		WHvX64RegisterRax, WHvX64RegisterRcx, WHvX64RegisterRdx, WHvX64RegisterRbx,
		WHvX64RegisterRsp, WHvX64RegisterRbp, WHvX64RegisterRsi, WHvX64RegisterRdi,
		WHvX64RegisterR8, WHvX64RegisterR9, WHvX64RegisterR10, WHvX64RegisterR11,
		WHvX64RegisterR12, WHvX64RegisterR13, WHvX64RegisterR14, WHvX64RegisterR15,
		WHvX64RegisterRip, WHvX64RegisterRflags,
		WHvX64RegisterEs, WHvX64RegisterCs, WHvX64RegisterSs, WHvX64RegisterDs, WHvX64RegisterFs, WHvX64RegisterGs,
		WHvX64RegisterCr0, WHvX64RegisterCr3, WHvX64RegisterCr4, WHvX64RegisterEfer,

	};

	static constexpr uint32_t state_count = sizeof ( state_names ) / sizeof ( state_names [ 0 ] );
	static constexpr uint32_t general_count = 18;

	static void append_text ( char* out, int32_t room, int32_t& used, const char* text ) {

		for ( int32_t i = 0; text [ i ] && used < room - 1; i++ )
			out [ used++ ] = text [ i ];

		out [ used ] = 0;

	}

	static void append_hex ( char* out, int32_t room, int32_t& used, uint64_t value, int32_t digits ) {

		static const char table [ ] = "0123456789ABCDEF";

		for ( int32_t shift = ( digits - 1 ) * 4; shift >= 0 && used < room - 1; shift -= 4 )
			out [ used++ ] = table [ value >> shift & 15 ];

		out [ used ] = 0;

	}

	machine::~machine ( ) {

		destroy ( );

	}

	bool machine::create ( uint32_t processor_count, uint64_t memory_bytes ) {

		destroy ( );
		problem [ 0 ] = 0;
		reason = STOP_NONE;

		if ( processor_count > max_processors )
			processor_count = max_processors;

		if ( core.create ( processor_count, memory_bytes ) )
			return true;

		int32_t used = 0;

		append_text ( problem, sizeof ( problem ), used, core.problem );

		return false;

	}

	void machine::destroy ( ) {

		stop ( );
		core.destroy ( );
		port_ranges.clear ( );
		memory_ranges.clear ( );

	}

	void machine::attach_ports ( uint16_t first, uint16_t last, device* target ) {

		port_ranges.push ( { first, last, target } );

	}

	void machine::attach_memory ( uint64_t base, uint64_t size, device* target ) {

		memory_ranges.push ( { base, size, target } );

	}

	bool machine::get_state ( uint32_t processor, cpu_state& out ) const {

		WHV_REGISTER_VALUE values [ state_count ] = { };

		if ( !core.registers ( processor, state_names, state_count, values ) )
			return false;

		for ( uint32_t i = 0; i < 16; i++ )
			out.gpr [ i ] = values [ i ].Reg64;

		out.rip = values [ 16 ].Reg64;
		out.rflags = values [ 17 ].Reg64;

		for ( uint32_t s = 0; s < SEG_COUNT; s++ ) {

			const WHV_X64_SEGMENT_REGISTER& source = values [ 18 + s ].Segment;

			out.segments [ s ] = { source.Base, source.Limit, source.Selector, source.Attributes };

		}

		out.cr0 = values [ 24 ].Reg64;
		out.cr3 = values [ 25 ].Reg64;
		out.cr4 = values [ 26 ].Reg64;
		out.efer = values [ 27 ].Reg64;

		return true;

	}

	bool machine::set_state ( uint32_t processor, const cpu_state& in ) {

		WHV_REGISTER_VALUE values [ state_count ] = { };

		for ( uint32_t i = 0; i < 16; i++ )
			values [ i ].Reg64 = in.gpr [ i ];

		values [ 16 ].Reg64 = in.rip;
		values [ 17 ].Reg64 = in.rflags;

		for ( uint32_t s = 0; s < SEG_COUNT; s++ ) {

			WHV_X64_SEGMENT_REGISTER& target = values [ 18 + s ].Segment;

			target.Base = in.segments [ s ].base;
			target.Limit = in.segments [ s ].limit;
			target.Selector = in.segments [ s ].selector;
			target.Attributes = in.segments [ s ].attributes;

		}

		values [ 24 ].Reg64 = in.cr0;
		values [ 25 ].Reg64 = in.cr3;
		values [ 26 ].Reg64 = in.cr4;
		values [ 27 ].Reg64 = in.efer;

		return core.set_registers ( processor, state_names, state_count, values );

	}

	bool machine::set_tables ( uint32_t processor, uint64_t gdt_base, uint16_t gdt_limit, uint64_t idt_base, uint16_t idt_limit ) {

		WHV_REGISTER_NAME names [ 2 ] = { WHvX64RegisterGdtr, WHvX64RegisterIdtr };
		WHV_REGISTER_VALUE values [ 2 ] = { };

		values [ 0 ].Table.Base = gdt_base;
		values [ 0 ].Table.Limit = gdt_limit;
		values [ 1 ].Table.Base = idt_base;
		values [ 1 ].Table.Limit = idt_limit;

		return core.set_registers ( processor, names, 2, values );

	}

	bool machine::park ( uint32_t processor ) {

		WHV_REGISTER_NAME name = WHvRegisterInternalActivityState;
		WHV_REGISTER_VALUE value = { };

		value.InternalActivity.StartupSuspend = 1;

		return core.set_registers ( processor, &name, 1, &value );

	}

	bool machine::start ( ) {

		if ( thread_count || !core.processors )
			return false;

		quit = 0;
		failed = 0;
		reason = STOP_NONE;

		for ( uint32_t i = 0; i < core.processors; i++ ) {

			processor_thread& slot = threads [ i ];

			slot.owner = this;
			slot.index = i;
			slot.wake = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
			slot.thread = slot.wake ? CreateThread ( nullptr, 0, &loop, &slot, 0, nullptr ) : nullptr;

			if ( !slot.thread ) {

				if ( slot.wake )
					CloseHandle ( slot.wake );

				slot.wake = nullptr;
				stop ( );

				return false;

			}

			thread_count = i + 1;

		}

		return true;

	}

	void machine::stop ( ) {

		if ( !thread_count )
			return;

		InterlockedExchange ( &quit, 1 );

		if ( reason == STOP_NONE )
			reason = STOP_REQUESTED;

		for ( uint32_t i = 0; i < thread_count; i++ ) {

			core.cancel ( i );
			SetEvent ( threads [ i ].wake );

		}

		for ( uint32_t i = 0; i < thread_count; i++ ) {

			WaitForSingleObject ( threads [ i ].thread, INFINITE );
			CloseHandle ( threads [ i ].thread );
			CloseHandle ( threads [ i ].wake );
			threads [ i ].thread = nullptr;
			threads [ i ].wake = nullptr;

		}

		thread_count = 0;

	}

	bool machine::interrupt ( uint32_t destination, uint32_t vector ) {

		WHV_INTERRUPT_CONTROL control = { };

		control.Type = WHvX64InterruptTypeFixed;
		control.DestinationMode = WHvX64InterruptDestinationModePhysical;
		control.TriggerMode = WHvX64InterruptTriggerModeEdge;
		control.Destination = destination;
		control.Vector = vector;

		bool sent = core.request ( control );

		if ( sent && destination < thread_count )
			SetEvent ( threads [ destination ].wake );

		return sent;

	}

	device* machine::find_memory ( uint64_t address, uint32_t size ) const {

		for ( const memory_range& range : memory_ranges ) {

			if ( address >= range.base && address - range.base + size <= range.size )
				return range.target;

		}

		return nullptr;

	}

	void machine::read ( uint64_t address, uint8_t* out, uint32_t size ) {

		if ( address < core.memory_size && size <= core.memory_size - address ) {

			for ( uint32_t i = 0; i < size; i++ )
				out [ i ] = core.memory [ address + i ];

			return;

		}

		device* target = find_memory ( address, size );
		uint64_t value = ~0ull;

		if ( target ) {

			AcquireSRWLockExclusive ( &device_lock );
			value = target->read ( address, size );
			ReleaseSRWLockExclusive ( &device_lock );

		}

		for ( uint32_t i = 0; i < size; i++ )
			out [ i ] = static_cast < uint8_t > ( value >> ( ( i & 7 ) * 8 ) );

	}

	void machine::write ( uint64_t address, const uint8_t* data, uint32_t size ) {

		if ( address < core.memory_size && size <= core.memory_size - address ) {

			for ( uint32_t i = 0; i < size; i++ )
				core.memory [ address + i ] = data [ i ];

			return;

		}

		device* target = find_memory ( address, size );

		if ( !target || size > 8 )
			return;

		uint64_t value = 0;

		for ( int32_t i = static_cast < int32_t > ( size ) - 1; i >= 0; i-- )
			value = value << 8 | data [ i ];

		AcquireSRWLockExclusive ( &device_lock );
		target->write ( address, size, value );
		ReleaseSRWLockExclusive ( &device_lock );

	}

	uint32_t machine::in ( uint16_t port, uint32_t size ) {

		for ( const port_range& range : port_ranges ) {

			if ( port >= range.first && port <= range.last ) {

				AcquireSRWLockExclusive ( &device_lock );

				uint32_t value = static_cast < uint32_t > ( range.target->read ( port, size ) );

				ReleaseSRWLockExclusive ( &device_lock );

				return value;

			}

		}

		return 0xFFFFFFFF;

	}

	void machine::out ( uint16_t port, uint32_t size, uint32_t value ) {

		for ( const port_range& range : port_ranges ) {

			if ( port >= range.first && port <= range.last ) {

				AcquireSRWLockExclusive ( &device_lock );
				range.target->write ( port, size, value );
				ReleaseSRWLockExclusive ( &device_lock );

				return;

			}

		}

	}

	DWORD __stdcall machine::loop ( void* context ) {

		processor_thread* slot = static_cast < processor_thread* > ( context );

		slot->owner->run_processor ( slot->index );

		return 0;

	}

	void machine::run_processor ( uint32_t index ) {

		while ( !quit ) {

			WHV_RUN_VP_EXIT_CONTEXT exit = { };

			if ( !core.run ( index, exit ) ) {

				fail ( index, "the hypervisor refused to run a processor", nullptr, 0 );

				return;

			}

			if ( !handle ( index, exit ) )
				return;

		}

	}

	bool machine::handle ( uint32_t index, const WHV_RUN_VP_EXIT_CONTEXT& exit ) {

		switch ( exit.ExitReason ) {

			case WHvRunVpExitReasonX64IoPortAccess: {

				const WHV_X64_IO_PORT_ACCESS_CONTEXT& io = exit.IoPortAccess;

				if ( io.AccessInfo.StringOp || io.AccessInfo.RepPrefix )
					return emulate_at ( index, io.InstructionBytes, io.InstructionByteCount );

				uint32_t size = io.AccessInfo.AccessSize;
				uint64_t mask = size >= 4 ? 0xFFFFFFFFull : ( 1ull << ( size * 8 ) ) - 1;

				if ( io.AccessInfo.IsWrite )
					out ( io.PortNumber, size, static_cast < uint32_t > ( io.Rax & mask ) );
				else {

					uint64_t value = in ( io.PortNumber, size ) & mask;
					WHV_REGISTER_NAME name = WHvX64RegisterRax;
					WHV_REGISTER_VALUE result = { };

					result.Reg64 = size >= 4 ? value : ( io.Rax & ~mask ) | value;

					if ( !core.set_registers ( index, &name, 1, &result ) )
						return fail ( index, "couldn't write the port result back", nullptr, 0 );

				}

				return core.skip ( index, exit ) || fail ( index, "couldn't step past a port access", nullptr, 0 );

			}

			case WHvRunVpExitReasonMemoryAccess:
				return emulate_at ( index, exit.MemoryAccess.InstructionBytes, exit.MemoryAccess.InstructionByteCount );

			case WHvRunVpExitReasonX64Halt:
				WaitForSingleObject ( threads [ index ].wake, 10 );
				return true;

			case WHvRunVpExitReasonCanceled:
				return true;

			case WHvRunVpExitReasonUnrecoverableException:
				reason = STOP_SHUTDOWN;
				return fail ( index, "the guest triple faulted", nullptr, 0 );

			default: {

				char text [ 64 ] = "the hypervisor stopped with exit ";
				int32_t used = 0;

				while ( text [ used ] )
					used++;

				append_hex ( text, sizeof ( text ), used, exit.ExitReason, 8 );

				return fail ( index, text, nullptr, 0 );

			}

		}

	}

	bool machine::emulate_at ( uint32_t index, const uint8_t* bytes, uint32_t count ) {

		cpu_state cpu;
		uint8_t fetched [ 16 ] = { };

		if ( !get_state ( index, cpu ) )
			return fail ( index, "couldn't read a processor's registers", nullptr, 0 );

		if ( !count ) {

			uint64_t where = cpu.code64 ( ) ? cpu.rip : ( cpu.segments [ SEG_CS ].base + cpu.rip ) & 0xFFFFFFFF;

			for ( uint32_t i = 0; i < 15; i++ ) {

				uint64_t physical = 0;

				if ( !translate ( cpu, *this, where + i, false, physical ) )
					break;

				read ( physical, fetched + i, 1 );
				count = i + 1;

			}

			bytes = fetched;

		}

		emulate_result result = emulate ( cpu, *this, bytes, count );

		if ( result == EMULATE_UNSUPPORTED )
			return fail ( index, "a device was touched by an instruction Coldboot can't emulate yet:", bytes, count );

		WHV_REGISTER_VALUE values [ general_count ] = { };

		for ( uint32_t i = 0; i < 16; i++ )
			values [ i ].Reg64 = cpu.gpr [ i ];

		values [ 16 ].Reg64 = cpu.rip;
		values [ 17 ].Reg64 = cpu.rflags;

		if ( !core.set_registers ( index, state_names, general_count, values ) )
			return fail ( index, "couldn't write a processor's registers", nullptr, 0 );

		return result == EMULATE_FAULT ? inject_fault ( index, cpu ) : true;

	}

	bool machine::inject_fault ( uint32_t index, const cpu_state& cpu ) {

		WHV_REGISTER_NAME names [ 2 ] = { WHvX64RegisterCr2, WHvRegisterPendingInterruption };
		WHV_REGISTER_VALUE values [ 2 ] = { };

		values [ 0 ].Reg64 = cpu.fault_address;
		values [ 1 ].PendingInterruption.InterruptionPending = 1;
		values [ 1 ].PendingInterruption.InterruptionType = WHvX64PendingException;
		values [ 1 ].PendingInterruption.DeliverErrorCode = 1;
		values [ 1 ].PendingInterruption.InterruptionVector = 14;
		values [ 1 ].PendingInterruption.ErrorCode = cpu.fault_code;

		return core.set_registers ( index, names, 2, values ) || fail ( index, "couldn't deliver a page fault to the guest", nullptr, 0 );

	}

	bool machine::fail ( uint32_t index, const char* text, const uint8_t* bytes, uint32_t count ) {

		if ( !InterlockedExchange ( &failed, 1 ) ) {

			int32_t used = 0;

			append_text ( problem, sizeof ( problem ), used, "processor " );
			append_hex ( problem, sizeof ( problem ), used, index, 2 );
			append_text ( problem, sizeof ( problem ), used, ": " );
			append_text ( problem, sizeof ( problem ), used, text );

			for ( uint32_t i = 0; bytes && i < count; i++ ) {

				append_text ( problem, sizeof ( problem ), used, " " );
				append_hex ( problem, sizeof ( problem ), used, bytes [ i ], 2 );

			}

			if ( reason != STOP_SHUTDOWN )
				reason = STOP_FAILED;

		}

		InterlockedExchange ( &quit, 1 );

		for ( uint32_t i = 0; i < thread_count; i++ ) {

			if ( i != index ) {

				core.cancel ( i );
				SetEvent ( threads [ i ].wake );

			}

		}

		return false;

	}

}