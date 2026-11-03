#pragma once

#include "pc/common.hh"
#include "pc/pci.hh"

namespace pc {

	enum vga_kind : uint8_t {

		VGA_TEXT,
		VGA_CGA,
		VGA_LINEAR,
		VGA_PLANAR,
		VGA_LFB,

	};

	struct vbe_mode {

		uint16_t number;
		uint16_t width;
		uint16_t height;
		uint8_t bits;

	};

	class vga : public hv::device, public pci_function {

	public:

		static constexpr uint64_t vram_size = 16ull * 1024 * 1024;
		static constexpr uint32_t default_lfb = 0xE0000000u;
		static constexpr int32_t max_width = 2560;
		static constexpr int32_t max_height = 1600;
		static const vbe_mode modes [ ];
		static const int32_t mode_count;

		explicit vga ( hv::machine& target );

		~vga ( ) override;

		void reset ( );

		void map ( );

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		void written ( uint32_t offset ) override;

		void set_legacy ( uint8_t mode );

		bool set_vbe ( uint16_t number );

		void set_font ( int32_t height );

		bool render ( uint32_t* out, int32_t& width, int32_t& height, double now );

		static const vbe_mode* find_mode ( uint16_t number );

		uint32_t lfb_address ( ) const {

			return mapped_at ? mapped_at : default_lfb;

		}

		void put_pixel ( uint32_t x, uint32_t y, uint8_t value, bool flip );

		uint8_t get_pixel ( uint32_t x, uint32_t y ) const;

		uint32_t planar_pitch ( ) const;

		uint8_t* vram = nullptr;
		uint8_t* planes = nullptr;
		uint8_t latches [ 4 ] = { };
		uint8_t font [ 256 ] [ 32 ] = { };
		uint8_t dac [ 256 ] [ 3 ] = { };
		uint8_t attributes [ 32 ] = { };
		uint8_t crtc [ 32 ] = { };
		uint8_t sequencer [ 8 ] = { };
		uint8_t graphics [ 16 ] = { };
		uint8_t misc = 0x67;
		uint8_t pel_mask = 0xFF;
		vga_kind kind = VGA_TEXT;
		uint8_t legacy_mode = 3;
		uint16_t vbe_number = 0;
		uint16_t vbe_width = 0;
		uint16_t vbe_height = 0;
		uint8_t vbe_bits = 0;
		uint32_t vbe_pitch = 0;
		uint32_t vbe_start = 0;
		uint8_t dac_bits = 6;
		int32_t columns = 80;
		int32_t rows = 25;
		int32_t char_height = 16;

	private:

		uint8_t read_port ( uint32_t port );

		void write_port ( uint32_t port, uint8_t value );

		uint8_t read_plane ( uint32_t offset );

		void write_plane ( uint32_t offset, uint8_t value );

		void default_palette ( );

		void load_dac ( bool wide );

		uint32_t color ( uint8_t index ) const;

		hv::machine& vm;
		uint32_t mapped_at = 0;
		uint8_t attribute_index = 0;
		bool attribute_flip = false;
		uint8_t sequencer_index = 0;
		uint8_t graphics_index = 0;
		uint8_t crtc_index = 0;
		uint8_t dac_write = 0;
		uint8_t dac_read = 0;
		uint8_t dac_part = 0;
		uint8_t dac_state = 0;

	};

}