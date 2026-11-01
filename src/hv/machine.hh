#pragma once

#include "hv/emulator.hh"
#include "hv/platform.hh"

namespace hv {

	struct device {

		virtual ~device ( ) = default;

		virtual uint64_t read ( uint64_t address, uint32_t size ) = 0;

		virtual void write ( uint64_t address, uint32_t size, uint64_t value ) = 0;

	};

	struct eoi_listener {

		virtual ~eoi_listener ( ) = default;

		virtual void eoi ( uint32_t vector ) = 0;

	};

	struct legacy_source {

		virtual ~legacy_source ( ) = default;

		virtual bool asserted ( ) = 0;

		virtual uint8_t acknowledge ( ) = 0;

	};

	enum stop_reason : uint8_t {

		STOP_NONE,
		STOP_REQUESTED,
		STOP_FAILED,
		STOP_SHUTDOWN,

	};

	class machine : public guest_bus {

	public:

		static constexpr uint32_t max_processors = 64;

		machine ( ) = default;

		machine ( const machine& ) = delete;

		machine& operator= ( const machine& ) = delete;

		~machine ( );

		bool create ( uint32_t processor_count, uint64_t memory_bytes );

		void destroy ( );

		void attach_ports ( uint16_t first, uint16_t last, device* target );

		void attach_memory ( uint64_t base, uint64_t size, device* target );

		bool get_state ( uint32_t processor, cpu_state& out ) const;

		bool set_state ( uint32_t processor, const cpu_state& in );

		bool set_tables ( uint32_t processor, uint64_t gdt_base, uint16_t gdt_limit, uint64_t idt_base, uint16_t idt_limit );

		bool park ( uint32_t processor );

		bool virtual_wire ( uint32_t processor );

		bool start ( );

		void stop ( );

		bool interrupt ( uint32_t destination, uint32_t vector, bool logical = false, bool level = false, uint32_t type = 0 );

		bool external ( uint32_t processor, uint32_t vector );

		void listen_eoi ( eoi_listener* target ) {

			eoi_target = target;

		}

		void set_legacy ( legacy_source* source ) {

			legacy = source;

		}

		void legacy_changed ( );

		void lock_devices ( ) {

			AcquireSRWLockExclusive ( &device_lock );

		}

		void unlock_devices ( ) {

			ReleaseSRWLockExclusive ( &device_lock );

		}

		bool pause ( );

		void resume ( );

		void read ( uint64_t address, uint8_t* out, uint32_t size ) override;

		void write ( uint64_t address, const uint8_t* data, uint32_t size ) override;

		uint32_t in ( uint16_t port, uint32_t size ) override;

		void out ( uint16_t port, uint32_t size, uint32_t value ) override;

		uint8_t* memory ( ) const {

			return core.memory;

		}

		bool running ( ) const {

			return thread_count != 0 && !quit;

		}

		partition core;
		stop_reason reason = STOP_NONE;
		volatile long msr_exits = 0;
		char problem [ 200 ] = { };

	private:

		struct port_range {

			uint16_t first;
			uint16_t last;
			device* target;

		};

		struct memory_range {

			uint64_t base;
			uint64_t size;
			device* target;

		};

		struct processor_thread {

			machine* owner = nullptr;
			uint32_t index = 0;
			HANDLE thread = nullptr;
			HANDLE wake = nullptr;

		};

		static DWORD __stdcall loop ( void* context );

		void run_processor ( uint32_t index );

		bool handle ( uint32_t index, const WHV_RUN_VP_EXIT_CONTEXT& exit );

		bool emulate_at ( uint32_t index, const uint8_t* bytes, uint32_t count );

		bool inject_fault ( uint32_t index, const cpu_state& cpu );

		bool raise ( uint32_t index, uint32_t vector, uint32_t code );

		bool deliver_external ( uint32_t index );

		bool read_msr ( uint32_t index, uint32_t msr, uint64_t& value ) const;

		bool write_msr ( uint32_t index, uint32_t msr, uint64_t value );

		bool fail ( uint32_t index, const char* text, const uint8_t* bytes, uint32_t count );

		device* find_memory ( uint64_t address, uint32_t size ) const;

		base::list < port_range > port_ranges;
		base::list < memory_range > memory_ranges;
		processor_thread threads [ max_processors ];
		volatile long pending [ max_processors ] = { };
		uint64_t mtrrs [ max_processors ] [ 28 ] = { };
		eoi_listener* eoi_target = nullptr;
		legacy_source* legacy = nullptr;
		volatile long legacy_wanted = 0;
		uint32_t thread_count = 0;
		volatile long quit = 0;
		volatile long failed = 0;
		volatile long pausing = 0;
		volatile long parked = 0;
		HANDLE resume_event = nullptr;
		HANDLE parked_event = nullptr;
		SRWLOCK device_lock = SRWLOCK_INIT;

	};

}