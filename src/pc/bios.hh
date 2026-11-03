#pragma once

#include "pc/ide.hh"
#include "pc/legacy.hh"
#include "pc/peripherals.hh"
#include "pc/vga.hh"

namespace pc {

	struct bios_frame {

		uint32_t eax = 0;
		uint32_t ebx = 0;
		uint32_t ecx = 0;
		uint32_t edx = 0;
		uint32_t esi = 0;
		uint32_t edi = 0;
		uint32_t ebp = 0;
		uint16_t ds = 0;
		uint16_t es = 0;
		uint16_t ip = 0;
		uint16_t cs = 0;
		uint16_t flags = 0;

		uint8_t al ( ) const {

			return static_cast < uint8_t > ( eax );

		}

		uint8_t ah ( ) const {

			return static_cast < uint8_t > ( eax >> 8 );

		}

		uint8_t bl ( ) const {

			return static_cast < uint8_t > ( ebx );

		}

		uint8_t bh ( ) const {

			return static_cast < uint8_t > ( ebx >> 8 );

		}

		uint8_t cl ( ) const {

			return static_cast < uint8_t > ( ecx );

		}

		uint8_t ch ( ) const {

			return static_cast < uint8_t > ( ecx >> 8 );

		}

		uint8_t dl ( ) const {

			return static_cast < uint8_t > ( edx );

		}

		uint8_t dh ( ) const {

			return static_cast < uint8_t > ( edx >> 8 );

		}

		void set_al ( uint8_t value ) {

			eax = ( eax & ~0xFFu ) | value;

		}

		void set_ah ( uint8_t value ) {

			eax = ( eax & ~0xFF00u ) | static_cast < uint32_t > ( value ) << 8;

		}

		void set_ax ( uint16_t value ) {

			eax = ( eax & ~0xFFFFu ) | value;

		}

		void set_bx ( uint16_t value ) {

			ebx = ( ebx & ~0xFFFFu ) | value;

		}

		void set_cx ( uint16_t value ) {

			ecx = ( ecx & ~0xFFFFu ) | value;

		}

		void set_dx ( uint16_t value ) {

			edx = ( edx & ~0xFFFFu ) | value;

		}

		void set_bl ( uint8_t value ) {

			ebx = ( ebx & ~0xFFu ) | value;

		}

		void set_bh ( uint8_t value ) {

			ebx = ( ebx & ~0xFF00u ) | static_cast < uint32_t > ( value ) << 8;

		}

		void set_cl ( uint8_t value ) {

			ecx = ( ecx & ~0xFFu ) | value;

		}

		void set_ch ( uint8_t value ) {

			ecx = ( ecx & ~0xFF00u ) | static_cast < uint32_t > ( value ) << 8;

		}

		void set_dl ( uint8_t value ) {

			edx = ( edx & ~0xFFu ) | value;

		}

		void set_dh ( uint8_t value ) {

			edx = ( edx & ~0xFF00u ) | static_cast < uint32_t > ( value ) << 8;

		}

		void carry ( bool value ) {

			flags = static_cast < uint16_t > ( value ? flags | 1 : flags & ~1 );

		}

		void zero ( bool value ) {

			flags = static_cast < uint16_t > ( value ? flags | 0x40 : flags & ~0x40 );

		}

	};

	struct bios_links {

		hv::machine* vm = nullptr;
		platform_bus* bus = nullptr;
		vga* video = nullptr;
		keyboard_controller* keyboard = nullptr;
		dual_pic* pics = nullptr;
		interval_timer* timer = nullptr;
		real_time_clock* clock = nullptr;
		pci_bus* pci = nullptr;
		serial_port* serial = nullptr;
		disk_file* hard_disk = nullptr;
		disk_file* optical = nullptr;
		uint32_t processors = 1;
		bool optical_first = true;
		uint64_t seed = 0;

	};

	class bios : public hv::device {

	public:

		static constexpr uint16_t port = 0x0E00;
		static constexpr uint32_t ebda = 0x9FC00;
		static constexpr uint8_t optical_drive = 0xE0;
		static constexpr uint32_t firmware_room = 0x40000;

		bool install ( const bios_links& links, char* error, int32_t room );

		uint64_t read ( uint64_t address, uint32_t size ) override;

		void write ( uint64_t address, uint32_t size, uint64_t value ) override;

		uint32_t entry_offset = 0xE000;
		int32_t boot_attempts = 0;
		char last_boot [ 64 ] = { };

	private:

		void execute ( uint16_t vector );

		bool need ( uint16_t segment, uint16_t offset, uint32_t bytes );

		void give ( uint16_t segment, uint16_t offset, const void* data, uint32_t bytes );

		uint8_t taken ( uint32_t index ) const {

			return index < input_size ? input [ index ] : 0;

		}

		uint8_t* memory ( uint32_t address, uint32_t size );

		uint8_t peek8 ( uint32_t address );

		uint16_t peek16 ( uint32_t address );

		uint32_t peek32 ( uint32_t address );

		void poke8 ( uint32_t address, uint8_t value );

		void poke16 ( uint32_t address, uint16_t value );

		void poke32 ( uint32_t address, uint32_t value );

		void copy_in ( uint32_t address, const void* data, uint32_t size );

		void copy_out ( uint32_t address, void* out, uint32_t size );

		uint32_t dispatch ( uint16_t vector, bios_frame& frame );

		void set_vector ( uint8_t vector, uint16_t segment, uint16_t offset );

		void build_tables ( );

		void build_video_rom ( );

		void reset_chips ( );

		void reset_bda ( );

		uint32_t video ( bios_frame& frame );

		uint32_t vbe ( bios_frame& frame );

		void set_mode ( uint8_t mode, bool clear_screen );

		void put_character ( uint8_t character, uint8_t attribute, uint8_t page, bool teletype, bool with_attribute );

		void scroll ( bool up, uint8_t lines, uint8_t attribute, uint8_t top, uint8_t left, uint8_t bottom, uint8_t right );

		void move_cursor ( uint8_t page, uint8_t row, uint8_t column );

		void print ( const char* text, uint8_t attribute );

		uint32_t disk ( bios_frame& frame );

		bool transfer ( uint8_t drive, uint64_t lba, uint32_t count, uint32_t address, bool writing, uint32_t& done );

		uint32_t system ( bios_frame& frame );

		uint32_t keyboard_service ( bios_frame& frame );

		void keyboard_interrupt ( );

		bool push_key ( uint16_t code );

		uint32_t time ( bios_frame& frame );

		uint32_t mouse_interrupt ( );

		uint32_t boot ( bios_frame& frame );

		bool boot_optical ( bios_frame& frame );

		bool boot_disk ( bios_frame& frame );

		uint32_t pci_service ( bios_frame& frame );

		static constexpr uint32_t transfer_room = 0x10000;
		static constexpr uint32_t status_needs_input = 0x100;

		bios_links link;
		bios_frame frame_state;
		uint16_t frame_words [ 19 ] = { };
		uint32_t frame_count = 0;
		uint32_t frame_cursor = 0;
		uint16_t requests [ 16 ] = { };
		uint32_t request_count = 0;
		uint32_t request_cursor = 0;
		uint8_t input [ transfer_room ] = { };
		uint32_t input_size = 0;
		uint32_t input_wanted = 0;
		uint16_t input_segment = 0;
		uint16_t input_offset = 0;
		bool input_ready = false;
		uint8_t output [ transfer_room ] = { };
		uint32_t output_size = 0;
		uint32_t output_cursor = 0;
		uint16_t output_segment = 0;
		uint16_t output_offset = 0;
		uint32_t vector_offsets [ 32 ] = { };
		uint32_t no_boot_offset = 0;
		uint32_t config_offset = 0;
		uint32_t disk_table_offset = 0;
		uint32_t video_table_offset = 0;
		uint32_t dummy_offset = 0;
		uint32_t emulated_lba = 0;
		uint8_t emulated_media = 0;
		bool emulating = false;
		uint32_t boot_image_lba = 0;
		uint16_t boot_load_segment = 0;
		uint16_t boot_sector_count = 0;
		uint8_t boot_media = 0;
		uint8_t mouse_bytes [ 3 ] = { };
		uint8_t mouse_count = 0;
		bool mouse_enabled = false;
		uint8_t last_disk_status = 0;
		bool extended_prefix = false;

	};

}