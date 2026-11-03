#pragma once

#include "pc/common.hh"
#include "pc/pci.hh"

namespace pc {

	class hda : public hv::device, public pci_function, public ticking {

	public:

		static constexpr uint32_t default_memory = 0xE1020000;
		static constexpr uint32_t memory_size = 0x4000;
		static constexpr uint32_t stream_count = 8;
		static constexpr uint32_t first_output = 4;

		hda ( platform_bus& owner, hv::machine& target, uint32_t line_number );

		~hda ( ) override;

		void connect ( sound_sink* target ) {

			speaker = target;

		}

		uint64_t read ( uint64_t address, uint32_t size ) override;

		void write ( uint64_t address, uint32_t size, uint64_t value ) override;

		void written ( uint32_t offset ) override;

		void advance ( uint64_t now ) override;

		uint32_t verb ( uint32_t command );

		uint32_t memory_base = default_memory;
		volatile long frames_played = 0;
		uint32_t verbs_run = 0;

	private:

		struct stream {

			uint32_t entry = 0;
			uint32_t entry_offset = 0;
			uint64_t carry = 0;
			uint64_t last = 0;

		};

		struct codec {

			uint16_t format = 0x0011;
			uint8_t stream_channel = 0;
			uint8_t gain [ 2 ] = { 0x4A, 0x4A };
			bool mute [ 2 ] = { false, false };
			bool pin_mute = false;
			uint8_t pin_control = 0x40;
			uint8_t power [ 4 ] = { };
			uint8_t eapd = 0x02;
			uint8_t unsolicited = 0;
			uint32_t config = 0x90170110;
			uint32_t subsystem = 0xCB010001;

		};

		void reset_controller ( );

		uint32_t get ( uint32_t offset, uint32_t size ) const;

		void put ( uint32_t offset, uint32_t size, uint32_t value );

		void touched ( uint32_t offset, uint32_t size, const uint8_t* before );

		void run_corb ( );

		void respond ( uint32_t response );

		uint32_t parameter ( uint32_t node, uint32_t id ) const;

		void reset_stream ( uint32_t index );

		void play_stream ( uint32_t index, uint64_t now );

		void update_line ( );

		platform_bus& bus;
		hv::machine& vm;
		sound_sink* speaker = nullptr;
		uint32_t irq = 17;
		uint8_t* regs = nullptr;
		float* mixed = nullptr;
		stream streams [ stream_count ];
		codec state;
		uint32_t responses = 0;
		uint64_t epoch = 0;
		bool line = false;

	};

}