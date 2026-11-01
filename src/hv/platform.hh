#pragma once

#include "base/base.hh"
#include <windows.h>
#include <WinHvPlatform.h>

namespace hv {

	struct functions {

		decltype ( &WHvGetCapability ) get_capability = nullptr;
		decltype ( &WHvCreatePartition ) create_partition = nullptr;
		decltype ( &WHvSetupPartition ) setup_partition = nullptr;
		decltype ( &WHvDeletePartition ) delete_partition = nullptr;
		decltype ( &WHvSetPartitionProperty ) set_property = nullptr;
		decltype ( &WHvMapGpaRange ) map_range = nullptr;
		decltype ( &WHvUnmapGpaRange ) unmap_range = nullptr;
		decltype ( &WHvCreateVirtualProcessor ) create_processor = nullptr;
		decltype ( &WHvDeleteVirtualProcessor ) delete_processor = nullptr;
		decltype ( &WHvRunVirtualProcessor ) run_processor = nullptr;
		decltype ( &WHvCancelRunVirtualProcessor ) cancel_processor = nullptr;
		decltype ( &WHvGetVirtualProcessorRegisters ) get_registers = nullptr;
		decltype ( &WHvSetVirtualProcessorRegisters ) set_registers = nullptr;
		decltype ( &WHvRequestInterrupt ) request_interrupt = nullptr;

	};

	const functions* platform ( );

	bool present ( );

	enum partition_feature : uint32_t {

		PARTITION_APIC = 1,
		PARTITION_EXITS = 2,

	};

	class partition {

	public:

		partition ( ) = default;

		partition ( const partition& ) = delete;

		partition& operator= ( const partition& ) = delete;

		~partition ( );

		bool create ( uint32_t processor_count, uint64_t memory_bytes, uint32_t features = PARTITION_APIC | PARTITION_EXITS );

		void destroy ( );

		bool cancel ( uint32_t processor );

		bool request ( const WHV_INTERRUPT_CONTROL& control );

		bool registers ( uint32_t processor, const WHV_REGISTER_NAME* names, uint32_t count, WHV_REGISTER_VALUE* values ) const;

		bool set_registers ( uint32_t processor, const WHV_REGISTER_NAME* names, uint32_t count, const WHV_REGISTER_VALUE* values );

		bool run ( uint32_t processor, WHV_RUN_VP_EXIT_CONTEXT& exit );

		bool skip ( uint32_t processor, const WHV_RUN_VP_EXIT_CONTEXT& exit );

		uint8_t* memory = nullptr;
		uint64_t memory_size = 0;
		uint32_t processors = 0;
		char problem [ 160 ] = { };

	private:

		bool fail ( const char* text, HRESULT code );

		WHV_PARTITION_HANDLE handle = nullptr;
		uint32_t created = 0;
		bool mapped = false;

	};

}