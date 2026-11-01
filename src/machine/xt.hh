#pragma once

#include "bus/bus.hh"
#include "chipset/dma.hh"
#include "chipset/pic.hh"
#include "chipset/pit.hh"
#include "chipset/ppi.hh"
#include "cpu/cpu.hh"
#include "input/keyboard.hh"
#include "storage/fdc.hh"
#include "video/cga.hh"

namespace machine {

	struct nmi_mask : port_device {

		uint8_t in ( uint16_t ) override {

			return 0xFF;

		}

		void out ( uint16_t, uint8_t value ) override {

			enabled = ( value & 0x80 ) != 0;

		}

		bool enabled = false;

	};

	class xt {

	public:

		static constexpr uint64_t clock_rate = 4772727;
		static constexpr uint32_t timer_rate = 1193182;
		static constexpr uint32_t sample_room = 16384;

		xt ( );

		xt ( const xt& ) = delete;

		xt& operator= ( const xt& ) = delete;

		void set_memory ( uint32_t kilobytes );

		bool load_bios ( const uint8_t* data, uint32_t size );

		bool load_builtin ( char* error, int32_t error_room, int32_t* error_line );

		void power_on ( );

		void run ( uint64_t cycles );

		int32_t take_samples ( float* out, int32_t room );

		void save_state ( base::list < uint8_t >& out );

		bool load_state ( const uint8_t* data, uint32_t size );

		bus memory;
		cpu core;
		pic interrupts;
		pit timer;
		dma transfers;
		dma_pages pages;
		keyboard keys;
		ppi peripheral;
		cga video;
		fdc floppy;
		nmi_mask nmi;
		uint32_t memory_kilobytes = 640;
		uint32_t sample_rate = 48000;
		bool bios_loaded = false;

	private:

		void advance ( uint32_t cycles );

		void transfer ( archive& io );

		float samples [ sample_room ] = { };
		int32_t sample_count = 0;
		uint32_t timer_phase = 0;
		uint32_t sample_phase = 0;
		uint32_t level_sum = 0;
		uint32_t level_ticks = 0;
		bool timer0 = false;
		bool timer1 = false;

	};

}