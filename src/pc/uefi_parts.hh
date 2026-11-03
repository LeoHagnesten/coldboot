#pragma once

#include "pc/uefi.hh"

namespace pc {

	namespace efi {

		static constexpr uint64_t fault = 0x8000000000000000ull;
		static constexpr uint64_t SUCCESS = 0;
		static constexpr uint64_t LOAD_ERROR = fault | 1;
		static constexpr uint64_t INVALID_PARAMETER = fault | 2;
		static constexpr uint64_t UNSUPPORTED = fault | 3;
		static constexpr uint64_t BAD_BUFFER_SIZE = fault | 4;
		static constexpr uint64_t BUFFER_TOO_SMALL = fault | 5;
		static constexpr uint64_t NOT_READY = fault | 6;
		static constexpr uint64_t DEVICE_ERROR = fault | 7;
		static constexpr uint64_t WRITE_PROTECTED = fault | 8;
		static constexpr uint64_t OUT_OF_RESOURCES = fault | 9;
		static constexpr uint64_t MEDIA_CHANGED = fault | 13;
		static constexpr uint64_t NOT_FOUND = fault | 14;

		enum memory_type : uint32_t {

			MEMORY_RESERVED,
			MEMORY_LOADER_CODE,
			MEMORY_LOADER_DATA,
			MEMORY_BOOT_CODE,
			MEMORY_BOOT_DATA,
			MEMORY_RUNTIME_CODE,
			MEMORY_RUNTIME_DATA,
			MEMORY_FREE,
			MEMORY_UNUSABLE,
			MEMORY_ACPI_RECLAIM,
			MEMORY_ACPI_NVS,
			MEMORY_MAPPED_IO,

		};

		enum service_id : uint32_t {

			BS_RAISE_TPL = 0,
			BS_RESTORE_TPL,
			BS_ALLOCATE_PAGES,
			BS_FREE_PAGES,
			BS_GET_MEMORY_MAP,
			BS_ALLOCATE_POOL,
			BS_FREE_POOL,
			BS_CREATE_EVENT,
			BS_SET_TIMER,
			BS_WAIT_FOR_EVENT,
			BS_SIGNAL_EVENT,
			BS_CLOSE_EVENT,
			BS_CHECK_EVENT,
			BS_INSTALL_PROTOCOL,
			BS_REINSTALL_PROTOCOL,
			BS_UNINSTALL_PROTOCOL,
			BS_HANDLE_PROTOCOL,
			BS_RESERVED,
			BS_REGISTER_NOTIFY,
			BS_LOCATE_HANDLE,
			BS_LOCATE_DEVICE_PATH,
			BS_INSTALL_CONFIGURATION,
			BS_LOAD_IMAGE,
			BS_START_IMAGE,
			BS_EXIT,
			BS_UNLOAD_IMAGE,
			BS_EXIT_BOOT_SERVICES,
			BS_MONOTONIC,
			BS_STALL,
			BS_WATCHDOG,
			BS_CONNECT,
			BS_DISCONNECT,
			BS_OPEN_PROTOCOL,
			BS_CLOSE_PROTOCOL,
			BS_PROTOCOL_INFORMATION,
			BS_PROTOCOLS_PER_HANDLE,
			BS_LOCATE_HANDLE_BUFFER,
			BS_LOCATE_PROTOCOL,
			BS_INSTALL_MULTIPLE,
			BS_UNINSTALL_MULTIPLE,
			BS_CRC32,
			BS_COPY_MEMORY,
			BS_SET_MEMORY,
			BS_CREATE_EVENT_EX,
			BOOT_SERVICE_COUNT,
			RT_GET_TIME = 64,
			RT_SET_TIME,
			RT_GET_WAKEUP,
			RT_SET_WAKEUP,
			RT_VIRTUAL_MAP,
			RT_CONVERT_POINTER,
			RT_GET_VARIABLE,
			RT_NEXT_VARIABLE,
			RT_SET_VARIABLE,
			RT_HIGH_COUNT,
			RT_RESET,
			RT_UPDATE_CAPSULE,
			RT_CAPSULE_CAPABILITIES,
			RT_VARIABLE_INFO,
			RUNTIME_SERVICE_END,
			IN_RESET = 100,
			IN_READ_KEY,
			IN_EX_RESET,
			IN_EX_READ_KEY,
			IN_EX_SET_STATE,
			IN_EX_REGISTER,
			IN_EX_UNREGISTER,
			OUT_RESET = 110,
			OUT_STRING,
			OUT_TEST,
			OUT_QUERY_MODE,
			OUT_SET_MODE,
			OUT_ATTRIBUTE,
			OUT_CLEAR,
			OUT_CURSOR,
			OUT_ENABLE_CURSOR,
			GOP_QUERY = 120,
			GOP_SET,
			GOP_BLT,
			BLOCK_RESET = 130,
			BLOCK_READ,
			BLOCK_WRITE,
			BLOCK_FLUSH,
			DISK_READ,
			DISK_WRITE,
			FIRMWARE_MAIN = 200,
			EXCEPTION_BASE = 0xE000,
			RESUME = 0xFFFFFFFF,

		};

		enum frame_kind : uint8_t {

			FRAME_RETURN,
			FRAME_IMAGE,
			FRAME_MAIN,
			FRAME_RETRY,

		};

		static constexpr uint32_t dispatch_resume = 21;
		static constexpr uint64_t page = 4096;

		extern const uint8_t guid_loaded_image [ 16 ];
		extern const uint8_t guid_image_path [ 16 ];
		extern const uint8_t guid_device_path [ 16 ];
		extern const uint8_t guid_block_io [ 16 ];
		extern const uint8_t guid_disk_io [ 16 ];
		extern const uint8_t guid_text_in [ 16 ];
		extern const uint8_t guid_text_in_ex [ 16 ];
		extern const uint8_t guid_text_out [ 16 ];
		extern const uint8_t guid_graphics [ 16 ];
		extern const uint8_t guid_acpi_20 [ 16 ];
		extern const uint8_t guid_acpi_10 [ 16 ];
		extern const uint8_t guid_smbios [ 16 ];
		extern const uint8_t guid_global [ 16 ];
		extern const uint8_t guid_exit_group [ 16 ];
		extern const uint8_t guid_virtual_group [ 16 ];
		extern const uint8_t guid_system_partition [ 16 ];

		inline void copy_text ( char* out, const char* text, int32_t room ) {

			int32_t used = 0;

			for ( ; text [ used ] && used < room - 1; used++ )
				out [ used ] = text [ used ];

			out [ used ] = 0;

		}

		bool same_guid ( const uint8_t* a, const uint8_t* b );

		uint32_t crc32 ( const uint8_t* data, uint64_t size, uint32_t seed );

	}

}