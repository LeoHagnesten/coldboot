#pragma once

#include "hv/machine.hh"
#include "pc/keyboard.hh"
#include "pc/legacy.hh"
#include "pc/storage.hh"
#include "pc/vga.hh"

namespace pc {

	struct uefi_links {

		hv::machine* vm = nullptr;
		platform_bus* bus = nullptr;
		vga* video = nullptr;
		keyboard_controller* keyboard = nullptr;
		dual_pic* pics = nullptr;
		disk_file* hard_disk = nullptr;
		disk_file* optical = nullptr;
		uint32_t processors = 1;
		bool optical_first = true;
		uint64_t seed = 0;
		const wchar_t* variables = nullptr;
		bool keep_variables = true;

	};

	class uefi : public hv::call_target {

	public:

		static constexpr uint16_t port = 0xE4;

		uefi ( );

		~uefi ( ) override;

		bool install ( const uefi_links& links, char* error, int32_t room );

		void call ( uint32_t processor, uint32_t value, hv::cpu_state& cpu ) override;

		char last_boot [ 128 ] = { };
		char trouble [ 160 ] = { };
		volatile long calls = 0;
		bool exited = false;

	private:

		struct installed {

			uint64_t handle = 0;
			uint8_t guid [ 16 ] = { };
			uint64_t interface = 0;

		};

		struct region {

			uint64_t start = 0;
			uint64_t pages = 0;
			uint32_t type = 0;

		};

		struct arena {

			uint32_t type = 0;
			uint64_t next = 0;
			uint64_t end = 0;

		};

		struct event {

			uint64_t handle = 0;
			uint32_t type = 0;
			uint64_t function = 0;
			uint64_t context = 0;
			uint64_t due = 0;
			uint64_t period = 0;
			uint8_t group = 0;
			bool signaled = false;
			bool keyboard = false;
			bool closed = false;

		};

		struct image {

			uint64_t handle = 0;
			uint64_t base = 0;
			uint64_t pages = 0;
			uint64_t entry = 0;
			uint64_t loaded = 0;

		};

		struct block_device {

			uint64_t handle = 0;
			uint64_t block_io = 0;
			uint64_t disk_io = 0;
			uint64_t path = 0;
			uint32_t path_size = 0;
			disk_file* media = nullptr;
			uint64_t offset = 0;
			uint64_t blocks = 0;
			uint32_t block_size = 512;
			bool read_only = false;
			bool partition = false;
			bool system = false;

		};

		struct frame {

			uint8_t kind = 0;
			uint64_t status = 0;
			uint64_t image = 0;
			hv::cpu_state saved;

		};

		struct callback {

			uint64_t function = 0;
			uint64_t first = 0;
			uint64_t second = 0;

		};

		struct variable {

			base::list < uint16_t > name;
			uint8_t guid [ 16 ] = { };
			uint32_t attributes = 0;
			base::list < uint8_t > data;

		};

		struct mapping {

			uint64_t physical = 0;
			uint64_t virtual_start = 0;
			uint64_t pages = 0;

		};

		bool to_physical ( hv::cpu_state& cpu, uint64_t address, bool write, uint64_t& physical );

		void get ( hv::cpu_state& cpu, uint64_t address, void* out, uint64_t size );

		void put ( hv::cpu_state& cpu, uint64_t address, const void* data, uint64_t size );

		uint64_t get64 ( hv::cpu_state& cpu, uint64_t address );

		uint32_t get32 ( hv::cpu_state& cpu, uint64_t address );

		void put64 ( hv::cpu_state& cpu, uint64_t address, uint64_t value );

		void put32 ( hv::cpu_state& cpu, uint64_t address, uint32_t value );

		void poke ( uint64_t address, const void* data, uint64_t size );

		void poke64 ( uint64_t address, uint64_t value );

		void poke32 ( uint64_t address, uint32_t value );

		void poke16 ( uint64_t address, uint16_t value );

		uint64_t argument ( hv::cpu_state& cpu, uint32_t index );

		uint64_t heap ( uint64_t size );

		uint64_t stub ( uint32_t id );

		uint64_t allocate ( uint32_t type, uint64_t pages, uint64_t below, uint64_t exact );

		bool release_pages ( uint64_t start, uint64_t pages );

		uint64_t pool ( uint32_t type, uint64_t size );

		void mark ( uint64_t start, uint64_t pages, uint32_t type );

		uint64_t new_handle ( );

		void install_protocol ( uint64_t handle, const uint8_t* guid, uint64_t interface );

		uint64_t find_protocol ( uint64_t handle, const uint8_t* guid ) const;

		bool known_handle ( uint64_t handle ) const;

		void build_memory ( );

		void build_tables ( );

		void build_console ( );

		void build_graphics ( );

		void build_disks ( );

		void add_block_device ( disk_file* media, uint64_t offset, uint64_t blocks, uint32_t block_size, bool read_only, bool partition, const uint8_t* path, uint32_t path_size, bool system );

		void build_variables ( );

		void load_variables ( );

		void save_variables ( );

		void set_variable ( const char* name, const uint8_t* guid, uint32_t attributes, const void* data, uint32_t size );

		bool load_image ( const uint8_t* file, uint32_t size, uint64_t parent, uint64_t device, const uint8_t* path, uint32_t path_size, uint64_t& handle, char* why );

		uint64_t service ( uint32_t id, hv::cpu_state& cpu, bool& handled );

		uint64_t boot_service ( uint32_t id, hv::cpu_state& cpu, bool& handled );

		uint64_t runtime_service ( uint32_t id, hv::cpu_state& cpu );

		uint64_t device_service ( uint32_t id, hv::cpu_state& cpu, bool& handled );

		void finish ( hv::cpu_state& cpu, uint64_t status );

		void start_callback ( hv::cpu_state& cpu, uint64_t function, uint64_t first, uint64_t second );

		void retry ( hv::cpu_state& cpu, uint32_t id );

		void continue_frame ( hv::cpu_state& cpu );

		void firmware_main ( hv::cpu_state& cpu );

		void wait_more ( hv::cpu_state& cpu, uint32_t id, bool& handled );

		uint32_t path_length ( hv::cpu_state& cpu, uint64_t address );

		bool has_protocol ( uint64_t handle, const uint8_t* guid, uint64_t& interface ) const;

		void collect_handles ( hv::cpu_state& cpu, uint32_t kind, uint64_t guid_address, base::list < uint64_t >& out );

		uint64_t match_device ( hv::cpu_state& cpu, uint64_t path, const uint8_t* guid, uint32_t& matched );

		bool read_file ( const block_device& device, const char* name, base::list < uint8_t >& out );

		bool next_boot ( char* why );

		void signal ( event& item );

		void check_timers ( );

		event* find_event ( uint64_t handle );

		bool poll_key ( uint16_t& scan, uint16_t& unicode );

		void pump_keyboard ( );

		void text_out ( const uint16_t* text, uint32_t count );

		void draw_cell ( int32_t column, int32_t row, uint16_t character );

		void scroll_text ( );

		void clear_text ( );

		void sync_text_mode ( );

		bool set_graphics ( uint32_t mode );

		uint64_t blt ( hv::cpu_state& cpu );

		block_device* find_block ( uint64_t interface, bool disk_io );

		uint64_t block_transfer ( hv::cpu_state& cpu, block_device& device, uint64_t byte_offset, uint64_t size, uint64_t buffer, bool writing );

		void log ( const char* text );

		hv::machine* vm = nullptr;
		uefi_links link;
		uint64_t firmware = 0;
		uint64_t firmware_size = 0;
		uint64_t stubs = 0;
		uint64_t heap_next = 0;
		uint64_t heap_end = 0;
		uint64_t system_table = 0;
		uint64_t boot_table = 0;
		uint64_t runtime_table = 0;
		uint64_t config_table = 0;
		uint32_t config_count = 0;
		uint64_t vendor = 0;
		uint64_t stack_top = 0;
		uint64_t console_handle = 0;
		uint64_t text_in = 0;
		uint64_t text_in_ex = 0;
		uint64_t text_out_interface = 0;
		uint64_t text_mode = 0;
		uint64_t key_event = 0;
		uint64_t graphics = 0;
		uint64_t graphics_mode = 0;
		uint64_t graphics_info = 0;
		uint64_t handle_next = 0;
		uint64_t map_key = 1;
		uint64_t monotonic = 0;
		uint64_t tpl = 4;
		uint64_t rsdp = 0;
		uint64_t smbios_entry = 0;
		base::list < installed > protocols;
		base::list < region > regions;
		base::list < arena > arenas;
		base::list < event > events;
		base::list < image > images;
		base::list < block_device > disks;
		base::list < frame > frames;
		base::list < callback > pending;
		base::list < variable > variables;
		base::list < mapping > mappings;
		uint64_t big_pools [ 256 ] [ 2 ] = { };
		uint16_t keys [ 32 ] [ 2 ] = { };
		uint32_t key_count = 0;
		bool shift = false;
		bool control = false;
		bool alt = false;
		bool extended = false;
		int32_t text_columns = 80;
		int32_t text_rows = 25;
		int32_t text_width = 128;
		int32_t text_height = 48;
		int32_t cursor_column = 0;
		int32_t cursor_row = 0;
		uint32_t text_attribute = 0x07;
		int32_t text_mode_number = 0;
		uint32_t graphics_number = 0;
		static constexpr uint32_t max_graphics = 32;
		uint16_t graphics_numbers [ max_graphics ] = { };
		uint32_t graphics_count = 0;
		int32_t boot_stage = 0;
		bool booted_disc = false;
		bool answer_prompt = false;
		bool virtual_mode = false;
		SRWLOCK lock = SRWLOCK_INIT;

	};

}