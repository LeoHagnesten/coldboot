#include "hv/platform.hh"

namespace hv {

	static functions table;
	static int32_t state = 0;

	template < typename type >
	static bool bind ( HMODULE module, const char* name, type& out ) {

		out = reinterpret_cast < type > ( GetProcAddress ( module, name ) );

		return out != nullptr;

	}

	const functions* platform ( ) {

		if ( state )
			return state > 0 ? &table : nullptr;

		HMODULE module = LoadLibraryExW ( L"WinHvPlatform.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );

		bool bound = module
			&& bind ( module, "WHvGetCapability", table.get_capability )
			&& bind ( module, "WHvCreatePartition", table.create_partition )
			&& bind ( module, "WHvSetupPartition", table.setup_partition )
			&& bind ( module, "WHvDeletePartition", table.delete_partition )
			&& bind ( module, "WHvSetPartitionProperty", table.set_property )
			&& bind ( module, "WHvMapGpaRange", table.map_range )
			&& bind ( module, "WHvUnmapGpaRange", table.unmap_range )
			&& bind ( module, "WHvCreateVirtualProcessor", table.create_processor )
			&& bind ( module, "WHvDeleteVirtualProcessor", table.delete_processor )
			&& bind ( module, "WHvRunVirtualProcessor", table.run_processor )
			&& bind ( module, "WHvCancelRunVirtualProcessor", table.cancel_processor )
			&& bind ( module, "WHvGetVirtualProcessorRegisters", table.get_registers )
			&& bind ( module, "WHvSetVirtualProcessorRegisters", table.set_registers )
			&& bind ( module, "WHvRequestInterrupt", table.request_interrupt );

		state = bound ? 1 : -1;

		return bound ? &table : nullptr;

	}

	bool present ( ) {

		const functions* api = platform ( );
		WHV_CAPABILITY capability = { };
		UINT32 written = 0;

		return api && SUCCEEDED ( api->get_capability ( WHvCapabilityCodeHypervisorPresent, &capability, sizeof ( capability ), &written ) ) && capability.HypervisorPresent;

	}

	partition::~partition ( ) {

		destroy ( );

	}

	bool partition::fail ( const char* text, HRESULT code ) {

		int32_t used = 0;

		for ( ; text [ used ] && used < static_cast < int32_t > ( sizeof ( problem ) ) - 14; used++ )
			problem [ used ] = text [ used ];

		if ( code ) {

			static const char digits [ ] = "0123456789ABCDEF";
			uint32_t value = static_cast < uint32_t > ( code );

			problem [ used++ ] = ' ';
			problem [ used++ ] = '(';
			problem [ used++ ] = '0';
			problem [ used++ ] = 'x';

			for ( int32_t shift = 28; shift >= 0; shift -= 4 )
				problem [ used++ ] = digits [ value >> shift & 15 ];

			problem [ used++ ] = ')';

		}

		problem [ used ] = 0;
		destroy ( );

		return false;

	}

	bool partition::create ( uint32_t processor_count, uint64_t memory_bytes, bool apic ) {

		destroy ( );
		problem [ 0 ] = 0;

		const functions* api = platform ( );

		if ( !api )
			return fail ( "The Windows Hypervisor Platform isn't installed on this PC", 0 );

		if ( !present ( ) )
			return fail ( "The Windows Hypervisor Platform is turned off", 0 );

		if ( !processor_count || processor_count > 64 || !memory_bytes )
			return fail ( "The machine needs between 1 and 64 processors and some memory", 0 );

		HRESULT result = api->create_partition ( &handle );

		if ( FAILED ( result ) )
			return fail ( "Couldn't create the partition", result );

		WHV_PARTITION_PROPERTY property = { };

		property.ProcessorCount = processor_count;
		result = api->set_property ( handle, WHvPartitionPropertyCodeProcessorCount, &property, sizeof ( property ) );

		if ( SUCCEEDED ( result ) && apic ) {

			WHV_PARTITION_PROPERTY mode = { };

			mode.LocalApicEmulationMode = WHvX64LocalApicEmulationModeXApic;
			result = api->set_property ( handle, WHvPartitionPropertyCodeLocalApicEmulationMode, &mode, sizeof ( mode ) );

			if ( FAILED ( result ) )
				return fail ( "This PC's hypervisor can't emulate the local APIC", result );

		}

		if ( SUCCEEDED ( result ) )
			result = api->setup_partition ( handle );

		if ( FAILED ( result ) )
			return fail ( "Couldn't set up the partition", result );

		memory_size = ( memory_bytes + 0xFFFF ) & ~static_cast < uint64_t > ( 0xFFFF );
		memory = static_cast < uint8_t* > ( VirtualAlloc ( nullptr, memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );

		if ( !memory )
			return fail ( "Couldn't allocate guest memory", HRESULT_FROM_WIN32 ( GetLastError ( ) ) );

		result = api->map_range ( handle, memory, 0, memory_size, static_cast < WHV_MAP_GPA_RANGE_FLAGS > ( WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute ) );

		if ( FAILED ( result ) )
			return fail ( "Couldn't map guest memory", result );

		mapped = true;

		for ( uint32_t i = 0; i < processor_count; i++ ) {

			result = api->create_processor ( handle, i, 0 );

			if ( FAILED ( result ) )
				return fail ( "Couldn't create a virtual processor", result );

			created = i + 1;

		}

		processors = processor_count;

		return true;

	}

	void partition::destroy ( ) {

		const functions* api = handle ? platform ( ) : nullptr;

		if ( api ) {

			for ( uint32_t i = created; i > 0; i-- )
				api->delete_processor ( handle, i - 1 );

			if ( mapped )
				api->unmap_range ( handle, 0, memory_size );

			api->delete_partition ( handle );

		}

		if ( memory )
			VirtualFree ( memory, 0, MEM_RELEASE );

		handle = nullptr;
		memory = nullptr;
		memory_size = 0;
		processors = 0;
		created = 0;
		mapped = false;

	}

	bool partition::registers ( uint32_t processor, const WHV_REGISTER_NAME* names, uint32_t count, WHV_REGISTER_VALUE* values ) const {

		return handle && processor < processors && SUCCEEDED ( platform ( )->get_registers ( handle, processor, names, count, values ) );

	}

	bool partition::set_registers ( uint32_t processor, const WHV_REGISTER_NAME* names, uint32_t count, const WHV_REGISTER_VALUE* values ) {

		return handle && processor < processors && SUCCEEDED ( platform ( )->set_registers ( handle, processor, names, count, values ) );

	}

	bool partition::run ( uint32_t processor, WHV_RUN_VP_EXIT_CONTEXT& exit ) {

		return handle && processor < processors && SUCCEEDED ( platform ( )->run_processor ( handle, processor, &exit, sizeof ( exit ) ) );

	}

	bool partition::cancel ( uint32_t processor ) {

		return handle && processor < processors && SUCCEEDED ( platform ( )->cancel_processor ( handle, processor, 0 ) );

	}

	bool partition::request ( const WHV_INTERRUPT_CONTROL& control ) {

		return handle && SUCCEEDED ( platform ( )->request_interrupt ( handle, &control, sizeof ( control ) ) );

	}

	bool partition::skip ( uint32_t processor, const WHV_RUN_VP_EXIT_CONTEXT& exit ) {

		WHV_REGISTER_NAME name = WHvX64RegisterRip;
		WHV_REGISTER_VALUE value = { };

		value.Reg64 = exit.VpContext.Rip + exit.VpContext.InstructionLength;

		return set_registers ( processor, &name, 1, &value );

	}

}