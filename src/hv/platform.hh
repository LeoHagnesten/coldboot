#pragma once

#include "base/base.hh"
#include <windows.h>
#include <WinHvPlatform.h>

namespace hv {

	typedef HRESULT ( WINAPI* apic_reader ) ( WHV_PARTITION_HANDLE partition, UINT32 processor, VOID* state, UINT32 size, UINT32* written );

	typedef HRESULT ( WINAPI* apic_writer ) ( WHV_PARTITION_HANDLE partition, UINT32 processor, const VOID* state, UINT32 size );

	struct functions {

		apic_reader get_apic = nullptr;
		apic_writer set_apic = nullptr;

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

		static constexpr uint64_t hole_start = 0xC0000000ull;
		static constexpr uint64_t high_start = 0x100000000ull;
		static constexpr int32_t max_windows = 4;

		partition ( ) = default;

		partition ( const partition& ) = delete;

		partition& operator= ( const partition& ) = delete;

		~partition ( );

		bool create ( uint32_t processor_count, uint64_t memory_bytes, uint32_t features = PARTITION_APIC | PARTITION_EXITS );

		void destroy ( );

		bool map_window ( uint8_t* host, uint64_t address, uint64_t size );

		void unmap_window ( uint64_t address );

		bool set_cap ( uint32_t percent );

		bool trap ( uint64_t address, uint64_t size );

		void untrap ( );

		uint8_t* host ( uint64_t address, uint64_t size ) const;

		uint64_t high_size ( ) const {

			return memory_size - low_size;

		}

		bool cancel ( uint32_t processor );

		bool request ( const WHV_INTERRUPT_CONTROL& control );

		bool registers ( uint32_t processor, const WHV_REGISTER_NAME* names, uint32_t count, WHV_REGISTER_VALUE* values ) const;

		bool set_registers ( uint32_t processor, const WHV_REGISTER_NAME* names, uint32_t count, const WHV_REGISTER_VALUE* values );

		bool run ( uint32_t processor, WHV_RUN_VP_EXIT_CONTEXT& exit );

		bool apic_page ( uint32_t processor, uint32_t* page ) const;

		bool set_apic_page ( uint32_t processor, const uint32_t* page );

		bool skip ( uint32_t processor, const WHV_RUN_VP_EXIT_CONTEXT& exit );

		uint8_t* memory = nullptr;
		uint64_t memory_size = 0;
		uint64_t low_size = 0;
		uint32_t processors = 0;
		char problem [ 160 ] = { };

	private:

		struct window {

			uint8_t* host;
			uint64_t address;
			uint64_t size;

		};

		bool fail ( const char* text, HRESULT code );

		WHV_PARTITION_HANDLE handle = nullptr;
		uint32_t created = 0;
		bool mapped = false;
		window windows [ max_windows ] = { };
		SRWLOCK window_lock = SRWLOCK_INIT;
		volatile uint64_t trapped_address = 0;
		volatile uint64_t trapped_size = 0;

	};

}