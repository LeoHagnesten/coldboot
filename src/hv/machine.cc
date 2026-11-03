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

	static int32_t mtrr_slot ( uint32_t msr ) {

		if ( msr >= 0x200 && msr <= 0x20F )
			return static_cast < int32_t > ( msr - 0x200 );

		if ( msr == 0x250 )
			return 16;

		if ( msr == 0x258 || msr == 0x259 )
			return static_cast < int32_t > ( 17 + msr - 0x258 );

		if ( msr >= 0x268 && msr <= 0x26F )
			return static_cast < int32_t > ( 19 + msr - 0x268 );

		return msr == 0x2FF ? 27 : -1;

	}

	static void shape_cpuid ( uint32_t leaf, uint32_t* result ) {

		if ( leaf == 1 ) {

			result [ 2 ] &= ~( ( 1u << 21 ) | ( 1u << 3 ) );
			result [ 2 ] |= 1u << 31;

		} else if ( leaf == 0x40000000 ) {

			result [ 0 ] = 0x40000001;
			result [ 1 ] = 0x646C6F43;
			result [ 2 ] = 0x746F6F62;
			result [ 3 ] = 0x20204D56;

		} else if ( leaf > 0x40000000 && leaf <= 0x400000FF ) {

			result [ 0 ] = 0;
			result [ 1 ] = 0;
			result [ 2 ] = 0;
			result [ 3 ] = 0;

		}

	}

	machine::~machine ( ) {

		destroy ( );

		if ( resume_event )
			CloseHandle ( resume_event );

		if ( parked_event )
			CloseHandle ( parked_event );

	}

	bool machine::create ( uint32_t processor_count, uint64_t memory_bytes ) {

		destroy ( );
		problem [ 0 ] = 0;
		reason = STOP_NONE;

		if ( processor_count > max_processors )
			processor_count = max_processors;

		for ( uint32_t i = 0; i < max_processors; i++ ) {

			for ( uint64_t& value : mtrrs [ i ] )
				value = 0;

		}

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

	void machine::relocate_ports ( device* target, uint16_t from, uint16_t first, uint16_t last ) {

		for ( port_range& range : port_ranges ) {

			if ( range.target == target && range.first == from ) {

				range.last = last;
				range.first = first;

				return;

			}

		}

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

	bool machine::virtual_wire ( uint32_t processor ) {

		uint32_t* page = static_cast < uint32_t* > ( runtime_allocate_zero ( 4096 ) );
		bool wired = core.apic_page ( processor, page );

		if ( wired ) {

			page [ 0xF0 / 4 ] = 0x1FF;
			page [ 0x350 / 4 ] = 0x700;
			page [ 0x360 / 4 ] = 0x400;
			wired = core.set_apic_page ( processor, page );

		}

		runtime_release ( page );

		if ( wired )
			return true;

		WHV_REGISTER_NAME names [ 3 ] = { WHvX64RegisterApicSpurious, WHvX64RegisterApicLvtLint0, WHvX64RegisterApicLvtLint1 };
		WHV_REGISTER_VALUE values [ 3 ] = { };

		values [ 0 ].Reg64 = 0x1FF;
		values [ 1 ].Reg64 = 0x700;
		values [ 2 ].Reg64 = 0x400;

		return core.set_registers ( processor, names, 3, values );

	}

	bool machine::start ( ) {

		if ( thread_count || !core.processors )
			return false;

		if ( !resume_event )
			resume_event = CreateEventW ( nullptr, TRUE, TRUE, nullptr );

		if ( !parked_event )
			parked_event = CreateEventW ( nullptr, TRUE, FALSE, nullptr );

		if ( !resume_event || !parked_event )
			return false;

		quit = 0;
		failed = 0;
		pausing = 0;
		parked = 0;
		reason = STOP_NONE;
		SetEvent ( resume_event );

		for ( uint32_t i = 0; i < max_processors; i++ ) {

			pending [ i ] = 0;
			window_seen [ i ] = false;

		}

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
		InterlockedExchange ( &pausing, 0 );
		SetEvent ( resume_event );

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

	bool machine::interrupt ( uint32_t destination, uint32_t vector, bool logical, bool level, uint32_t type ) {

		WHV_INTERRUPT_CONTROL control = { };

		control.Type = type;
		control.DestinationMode = logical ? WHvX64InterruptDestinationModeLogical : WHvX64InterruptDestinationModePhysical;
		control.TriggerMode = level ? WHvX64InterruptTriggerModeLevel : WHvX64InterruptTriggerModeEdge;
		control.Destination = destination;
		control.Vector = vector;

		bool sent = core.request ( control );

		for ( uint32_t i = 0; sent && i < thread_count; i++ ) {

			if ( logical || i == destination || destination == 0xFF )
				SetEvent ( threads [ i ].wake );

		}

		return sent;

	}

	bool machine::external ( uint32_t processor, uint32_t vector ) {

		if ( processor >= core.processors )
			return false;

		InterlockedExchange ( &pending [ processor ], static_cast < long > ( 0x100 | ( vector & 0xFF ) ) );

		if ( processor < thread_count ) {

			core.cancel ( processor );
			SetEvent ( threads [ processor ].wake );

		}

		return true;

	}

	void machine::legacy_changed ( ) {

		if ( !legacy || !legacy->asserted ( ) )
			return;

		InterlockedExchange ( &legacy_wanted, 1 );

		if ( thread_count ) {

			core.cancel ( 0 );
			SetEvent ( threads [ 0 ].wake );

		}

	}

	bool machine::pause ( ) {

		if ( !thread_count || quit )
			return false;

		ResetEvent ( parked_event );
		ResetEvent ( resume_event );
		InterlockedExchange ( &pausing, 1 );

		for ( uint32_t waited = 0; waited < 2000; waited++ ) {

			for ( uint32_t i = 0; i < thread_count; i++ ) {

				core.cancel ( i );
				SetEvent ( threads [ i ].wake );

			}

			if ( WaitForSingleObject ( parked_event, 1 ) == WAIT_OBJECT_0 )
				return true;

			if ( quit )
				break;

		}

		resume ( );

		return false;

	}

	void machine::resume ( ) {

		InterlockedExchange ( &pausing, 0 );
		SetEvent ( resume_event );

	}

	device* machine::find_memory ( uint64_t address, uint32_t size ) const {

		for ( const memory_range& range : memory_ranges ) {

			if ( address >= range.base && address - range.base + size <= range.size )
				return range.target;

		}

		return nullptr;

	}

	void machine::read ( uint64_t address, uint8_t* out, uint32_t size ) {

		if ( const uint8_t* direct = core.host ( address, size ) ) {

			for ( uint32_t i = 0; i < size; i++ )
				out [ i ] = direct [ i ];

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

		if ( uint8_t* direct = core.host ( address, size ) ) {

			for ( uint32_t i = 0; i < size; i++ )
				direct [ i ] = data [ i ];

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

			if ( pausing ) {

				if ( InterlockedIncrement ( &parked ) == static_cast < long > ( thread_count ) )
					SetEvent ( parked_event );

				WaitForSingleObject ( resume_event, INFINITE );
				InterlockedDecrement ( &parked );

				continue;

			}

			if ( ( pending [ index ] || ( index == 0 && legacy_wanted ) ) && !deliver_external ( index ) )
				return;

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

			case WHvRunVpExitReasonX64Cpuid: {

				const WHV_X64_CPUID_ACCESS_CONTEXT& access = exit.CpuidAccess;
				uint32_t result [ 4 ] = { static_cast < uint32_t > ( access.DefaultResultRax ), static_cast < uint32_t > ( access.DefaultResultRbx ), static_cast < uint32_t > ( access.DefaultResultRcx ), static_cast < uint32_t > ( access.DefaultResultRdx ) };
				WHV_REGISTER_NAME names [ 4 ] = { WHvX64RegisterRax, WHvX64RegisterRbx, WHvX64RegisterRcx, WHvX64RegisterRdx };
				WHV_REGISTER_VALUE values [ 4 ] = { };

				shape_cpuid ( static_cast < uint32_t > ( access.Rax ), result );

				for ( uint32_t i = 0; i < 4; i++ )
					values [ i ].Reg64 = result [ i ];

				if ( !core.set_registers ( index, names, 4, values ) )
					return fail ( index, "couldn't write a CPUID result back", nullptr, 0 );

				return core.skip ( index, exit ) || fail ( index, "couldn't step past CPUID", nullptr, 0 );

			}

			case WHvRunVpExitReasonX64MsrAccess: {

				const WHV_X64_MSR_ACCESS_CONTEXT& access = exit.MsrAccess;

				InterlockedIncrement ( &msr_exits );
				last_msr = access.MsrNumber;

				if ( access.AccessInfo.IsWrite ) {

					if ( !write_msr ( index, access.MsrNumber, ( access.Rdx << 32 ) | ( access.Rax & 0xFFFFFFFF ) ) )
						return raise ( index, 13, 0 );

				} else {

					uint64_t value = 0;

					if ( !read_msr ( index, access.MsrNumber, value ) )
						return raise ( index, 13, 0 );

					WHV_REGISTER_NAME names [ 2 ] = { WHvX64RegisterRax, WHvX64RegisterRdx };
					WHV_REGISTER_VALUE values [ 2 ] = { };

					values [ 0 ].Reg64 = value & 0xFFFFFFFF;
					values [ 1 ].Reg64 = value >> 32;

					if ( !core.set_registers ( index, names, 2, values ) )
						return fail ( index, "couldn't write an MSR value back", nullptr, 0 );

				}

				return core.skip ( index, exit ) || fail ( index, "couldn't step past an MSR access", nullptr, 0 );

			}

			case WHvRunVpExitReasonX64ApicEoi:

				if ( eoi_target ) {

					AcquireSRWLockExclusive ( &device_lock );
					eoi_target->eoi ( exit.ApicEoi.InterruptVector );
					ReleaseSRWLockExclusive ( &device_lock );

				}

				return true;

			case WHvRunVpExitReasonX64InterruptWindow:
				window_seen [ index ] = true;
				return true;

			case WHvRunVpExitReasonX64Halt:

				if ( !pending [ index ] && !( index == 0 && legacy_wanted ) )
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

		WHV_REGISTER_NAME name = WHvX64RegisterCr2;
		WHV_REGISTER_VALUE value = { };

		value.Reg64 = cpu.fault_address;

		if ( !core.set_registers ( index, &name, 1, &value ) )
			return fail ( index, "couldn't set CR2 for a page fault", nullptr, 0 );

		return raise ( index, 14, cpu.fault_code );

	}

	bool machine::raise ( uint32_t index, uint32_t vector, uint32_t code ) {

		WHV_REGISTER_NAME name = WHvRegisterPendingInterruption;
		WHV_REGISTER_VALUE value = { };

		value.PendingInterruption.InterruptionPending = 1;
		value.PendingInterruption.InterruptionType = WHvX64PendingException;
		value.PendingInterruption.DeliverErrorCode = 1;
		value.PendingInterruption.InterruptionVector = vector;
		value.PendingInterruption.ErrorCode = code;

		return core.set_registers ( index, &name, 1, &value ) || fail ( index, "couldn't deliver an exception to the guest", nullptr, 0 );

	}

	bool machine::deliver_external ( uint32_t index ) {

		WHV_REGISTER_NAME names [ 4 ] = { WHvRegisterPendingEvent, WHvX64RegisterRflags, WHvRegisterInterruptState, WHvRegisterInternalActivityState };
		WHV_REGISTER_VALUE values [ 4 ] = { };

		if ( !core.registers ( index, names, 4, values ) )
			return fail ( index, "couldn't read the interrupt state", nullptr, 0 );

		bool open = window_seen [ index ] || ( ( values [ 1 ].Reg64 & 0x200 ) && !values [ 2 ].InterruptState.InterruptShadow );

		window_seen [ index ] = false;

		if ( values [ 0 ].ExtIntEvent.EventPending || !open ) {

			WHV_REGISTER_NAME window = WHvX64RegisterDeliverabilityNotifications;
			WHV_REGISTER_VALUE notify = { };

			notify.DeliverabilityNotifications.InterruptNotification = 1;

			return core.set_registers ( index, &window, 1, &notify ) || fail ( index, "couldn't ask for an interrupt window", nullptr, 0 );

		}

		long vector = InterlockedExchange ( &pending [ index ], 0 );

		if ( !vector && index == 0 && legacy ) {

			AcquireSRWLockExclusive ( &device_lock );

			if ( legacy->asserted ( ) )
				vector = 0x100 | legacy->acknowledge ( );

			if ( !legacy->asserted ( ) )
				InterlockedExchange ( &legacy_wanted, 0 );

			ReleaseSRWLockExclusive ( &device_lock );

		}

		if ( !vector )
			return true;

		WHV_REGISTER_NAME updates [ 2 ] = { WHvRegisterPendingEvent, WHvRegisterInternalActivityState };
		WHV_REGISTER_VALUE changes [ 2 ] = { };

		InterlockedIncrement ( &injected );
		changes [ 0 ].ExtIntEvent.EventPending = 1;
		changes [ 0 ].ExtIntEvent.EventType = WHvX64PendingEventExtInt;
		changes [ 0 ].ExtIntEvent.Vector = static_cast < uint64_t > ( vector & 0xFF );
		changes [ 1 ] = values [ 3 ];
		changes [ 1 ].InternalActivity.HaltSuspend = 0;

		return core.set_registers ( index, updates, 2, changes ) || fail ( index, "couldn't queue an external interrupt", nullptr, 0 );

	}

	bool machine::read_msr ( uint32_t index, uint32_t msr, uint64_t& value ) const {

		int32_t slot = mtrr_slot ( msr );

		if ( slot >= 0 ) {

			value = mtrrs [ index ] [ slot ];

			return true;

		}

		switch ( msr ) {

			case 0x0000003A:
				value = 1;
				return true;

			case 0x0000008B:
			case 0x00000179:
			case 0x0000017A:
				value = 0;
				return true;

			case 0x000000FE:
				value = 0x508;
				return true;

			case 0x000001A0:
				value = 1;
				return true;

			default:
				value = 0;
				return lenient_msrs;

		}

	}

	bool machine::write_msr ( uint32_t index, uint32_t msr, uint64_t value ) {

		int32_t slot = mtrr_slot ( msr );

		if ( slot >= 0 ) {

			mtrrs [ index ] [ slot ] = value;

			return true;

		}

		return msr == 0x0000008B || msr == 0x0000017A || msr == 0x000001A0 || lenient_msrs;

	}

	void machine::set_mtrr ( uint32_t msr, uint64_t value ) {

		int32_t slot = mtrr_slot ( msr );

		for ( uint32_t i = 0; slot >= 0 && i < max_processors; i++ )
			mtrrs [ i ] [ slot ] = value;

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