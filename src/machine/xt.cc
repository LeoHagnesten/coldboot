#include "machine/xt.hh"

namespace machine {

	xt::xt ( ) : core ( memory ), pages ( transfers ), peripheral ( keys, timer ), video ( memory ), floppy ( transfers, memory ) {

		set_memory ( memory_kilobytes );
		peripheral.switches = 0x6D;
		memory.attach ( 0x00, 0x1F, &transfers );
		memory.attach ( 0x20, 0x3F, &interrupts );
		memory.attach ( 0x40, 0x5F, &timer );
		memory.attach ( 0x60, 0x7F, &peripheral );
		memory.attach ( 0x80, 0x9F, &pages );
		memory.attach ( 0xA0, 0xBF, &nmi );
		memory.attach ( 0x3D0, 0x3DF, &video );
		memory.attach ( 0x3F0, 0x3F7, &floppy );
		core.controller = &interrupts;
		video.clock = &core.cycles;
		power_on ( );

	}

	void xt::set_memory ( uint32_t kilobytes ) {

		memory_kilobytes = base::clamp ( kilobytes, 16u, 640u ) & ~15u;
		memory.map ( 0, bus::memory_size, PAGE_OPEN, 0 );
		memory.map ( 0, memory_kilobytes * 1024, PAGE_RAM, 0 );
		memory.map ( 0xB8000, 0x4000, PAGE_RAM, 0xB8000 );
		memory.map ( 0xBC000, 0x4000, PAGE_RAM, 0xB8000 );
		bios_loaded = false;

	}

	bool xt::load_bios ( const uint8_t* data, uint32_t size ) {

		if ( !data || !size || size > 0x10000 )
			return false;

		uint32_t rounded = ( size + bus::page_size - 1 ) & ~( bus::page_size - 1 );
		uint8_t* image = static_cast < uint8_t* > ( runtime_allocate ( rounded ) );

		for ( uint32_t i = 0; i < rounded - size; i++ )
			image [ i ] = 0xFF;

		for ( uint32_t i = 0; i < size; i++ )
			image [ rounded - size + i ] = data [ i ];

		bios_loaded = memory.load_rom ( 0x100000 - rounded, image, rounded );
		runtime_release ( image );

		return bios_loaded;

	}

	void xt::power_on ( ) {

		memory.clear ( );
		core.reset ( );
		interrupts.reset ( );
		timer.reset ( );
		transfers.reset ( );
		keys.reset ( );
		peripheral.reset ( );
		video.reset ( );
		floppy.reset ( );
		nmi.enabled = false;
		timer_phase = 0;
		sample_phase = 0;
		level_sum = 0;
		level_ticks = 0;
		sample_count = 0;
		timer0 = timer.channels [ 0 ].output;
		timer1 = timer.channels [ 1 ].output;
		interrupts.set_line ( 0, timer0 );

	}

	void xt::advance ( uint32_t cycles ) {

		timer_phase += cycles;

		while ( timer_phase >= 4 ) {

			timer_phase -= 4;
			timer.clock ( );

			bool out0 = timer.channels [ 0 ].output;
			bool out1 = timer.channels [ 1 ].output;

			if ( out0 != timer0 ) {

				timer0 = out0;
				interrupts.set_line ( 0, out0 );

			}

			if ( out1 && !timer1 ) {

				uint8_t unused = 0;

				transfers.transfer ( 0, memory, unused );

			}

			timer1 = out1;
			level_sum += peripheral.speaker ( ) ? 1 : 0;
			level_ticks++;
			sample_phase += sample_rate;

			if ( sample_phase < timer_rate )
				continue;

			sample_phase -= timer_rate;

			if ( sample_count < static_cast < int32_t > ( sample_room ) )
				samples [ sample_count++ ] = static_cast < float > ( level_sum ) / static_cast < float > ( level_ticks ) - 0.5f;

			level_sum = 0;
			level_ticks = 0;

		}

		keys.advance ( cycles );
		interrupts.set_line ( 1, keys.full );
		floppy.advance ( cycles );
		interrupts.set_line ( 6, floppy.interrupt ( ) );

	}

	void xt::run ( uint64_t cycles ) {

		uint64_t target = core.cycles + cycles;

		while ( core.cycles < target ) {

			uint64_t before = core.cycles;

			core.step ( );
			advance ( static_cast < uint32_t > ( core.cycles - before ) );

		}

		video.update ( core.cycles );

	}

	int32_t xt::take_samples ( float* out, int32_t room ) {

		int32_t count = base::min ( room, sample_count );

		for ( int32_t i = 0; i < count; i++ )
			out [ i ] = samples [ i ];

		for ( int32_t i = count; i < sample_count; i++ )
			samples [ i - count ] = samples [ i ];

		sample_count -= count;

		return count;

	}

}