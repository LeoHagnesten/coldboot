#pragma once

#include "pc/common.hh"
#include "pc/pci.hh"
#include "pc/storage.hh"

namespace pc {

	enum drive_kind : uint8_t {

		DRIVE_NONE,
		DRIVE_DISK,
		DRIVE_OPTICAL,

	};

	enum transfer_kind : uint8_t {

		TRANSFER_NONE,
		TRANSFER_READ,
		TRANSFER_WRITE,
		TRANSFER_REPLY,
		TRANSFER_PACKET,
		TRANSFER_PACKET_IN,
		TRANSFER_PACKET_OUT,

	};

	struct ide_drive {

		drive_kind kind = DRIVE_NONE;
		disk_file* media = nullptr;
		uint64_t sectors = 0;
		uint16_t cylinders = 0;
		uint16_t heads = 16;
		uint16_t per_track = 63;
		uint8_t multiple = 16;
		uint8_t transfer_mode = 0x0C;
		uint8_t sense_key = 0;
		uint8_t sense_code = 0;
		uint8_t sense_qualifier = 0;
		bool attention = false;
		bool media_event = false;
		bool locked = false;
		bool write_cache = true;

	};

	struct ide_channel {

		static constexpr uint32_t buffer_size = 0x20000;

		ide_drive drives [ 2 ];
		uint8_t unit = 0;
		uint8_t features = 0;
		uint8_t error = 0;
		uint8_t count [ 2 ] = { };
		uint8_t lba [ 3 ] [ 2 ] = { };
		uint8_t device = 0xA0;
		uint8_t status = 0;
		uint8_t control = 0;
		uint8_t command = 0;
		bool interrupt = false;
		bool line = false;
		transfer_kind transfer = TRANSFER_NONE;
		uint8_t* buffer = nullptr;
		uint32_t position = 0;
		uint32_t limit = 0;
		uint32_t filled = 0;
		uint32_t unit_size = 512;
		uint64_t next_lba = 0;
		uint32_t remaining = 0;
		uint32_t block = 1;
		uint64_t packet_bytes = 0;
		uint32_t byte_limit = 0;
		bool stream = false;
		bool dma_pending = false;
		bool dma_to_memory = false;
		uint8_t packet [ 12 ] = { };
		uint8_t master_command = 0;
		uint8_t master_status = 0;
		uint32_t table = 0;

	};

	class ide_controller : public hv::device, public pci_function {

	public:

		static constexpr uint16_t default_master = 0xC000;

		ide_controller ( platform_bus& owner, hv::machine& target );

		~ide_controller ( ) override;

		void reset ( );

		void attach ( int32_t channel, int32_t unit, drive_kind kind, disk_file* media );

		void change_media ( int32_t channel, int32_t unit, disk_file* media );

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		void written ( uint32_t offset ) override;

		uint16_t master_base = default_master;
		volatile long busy_light = 0;

	private:

		ide_drive& selected ( ide_channel& channel ) {

			return channel.drives [ channel.unit ];

		}

		uint64_t read_register ( int32_t index, uint32_t port, uint32_t size );

		void write_register ( int32_t index, uint32_t port, uint32_t size, uint32_t value );

		uint64_t read_master ( uint32_t offset, uint32_t size );

		void write_master ( uint32_t offset, uint32_t size, uint32_t value );

		void raise ( int32_t index );

		void lower ( int32_t index );

		void update_line ( int32_t index );

		void signature ( int32_t index, int32_t unit );

		void soft_reset ( int32_t index );

		void execute ( int32_t index, uint8_t code );

		void abort_command ( int32_t index );

		void finish ( int32_t index, bool interrupt );

		uint64_t address ( ide_channel& channel, bool extended ) const;

		uint32_t sector_count ( ide_channel& channel, bool extended ) const;

		void set_address ( ide_channel& channel, uint64_t value, bool extended );

		void start_read ( int32_t index, uint64_t lba, uint32_t count, uint32_t block );

		void fill_block ( int32_t index );

		void start_write ( int32_t index, uint64_t lba, uint32_t count, uint32_t block );

		void block_written ( int32_t index );

		void identify_disk ( int32_t index );

		void identify_packet ( int32_t index );

		void data_done ( int32_t index );

		void packet_command ( int32_t index );

		void packet_reply ( int32_t index, uint32_t length, uint32_t allowed );

		void packet_stream ( int32_t index, uint64_t lba, uint32_t blocks );

		void packet_chunk ( int32_t index );

		void packet_good ( int32_t index );

		void packet_error ( int32_t index, uint8_t key, uint8_t code, uint8_t qualifier );

		void try_dma ( int32_t index );

		uint32_t produce ( ide_channel& channel, uint8_t* out, uint32_t room );

		void consume ( ide_channel& channel, const uint8_t* data, uint32_t size );

		uint32_t mode_page ( uint8_t page, uint8_t* out );

		uint32_t table_of_contents ( ide_drive& drive, const uint8_t* packet, uint8_t* out );

		platform_bus& bus;
		hv::machine& vm;
		ide_channel channels [ 2 ];

	};

}