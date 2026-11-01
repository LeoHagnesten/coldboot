#pragma once

#include "bus/bus.hh"
#include "chipset/dma.hh"
#include "storage/floppy.hh"

namespace machine {

	enum fdc_phase : uint8_t {

		FDC_COMMAND,
		FDC_EXECUTION,
		FDC_RESULT,

	};

	enum fdc_task : uint8_t {

		TASK_NONE,
		TASK_READ,
		TASK_WRITE,
		TASK_FORMAT,
		TASK_IDENTIFY,

	};

	struct floppy_drive {

		floppy disk;
		uint32_t seek_wait = 0;
		uint8_t cylinder = 0;
		uint8_t target = 0;
		uint8_t side = 0;
		uint8_t status = 0;
		bool seeking = false;
		bool calibrating = false;
		bool reported = false;
		bool attached = false;

	};

	class fdc : public port_device {

	public:

		static constexpr int32_t drive_count = 4;
		static constexpr uint32_t millisecond = 4773;

		uint32_t byte_cycles ( ) const {

			return fast ? 12 : 153;

		}

		uint32_t settle_cycles ( ) const {

			return fast ? millisecond / 8 : millisecond * 2;

		}

		fdc ( dma& transfers, bus& memory );

		void reset ( );

		void advance ( uint32_t cycles );

		bool insert ( int32_t index, const uint8_t* data, uint32_t size, bool protect );

		void eject ( int32_t index );

		bool interrupt ( ) const {

			return pending && ( output & 0x08 );

		}

		bool spinning ( int32_t index ) const {

			return ( output & 0x04 ) && ( output >> ( 4 + index ) & 1 );

		}

		uint8_t in ( uint16_t port ) override;

		void out ( uint16_t port, uint8_t value ) override;

		void transfer ( archive& io );

		floppy_drive drives [ drive_count ];
		uint8_t output = 0;
		uint8_t step_rate = 0x0C;
		fdc_phase phase = FDC_COMMAND;
		bool fast = false;

	private:

		void clear_controller ( );

		void write_output ( uint8_t value );

		void write_data ( uint8_t value );

		uint8_t read_data ( );

		uint8_t main_status ( ) const;

		void execute ( );

		void respond ( uint8_t count, bool clears );

		void seek ( uint8_t index, uint8_t cylinder, bool calibrate );

		void sense ( );

		void start ( fdc_task kind );

		void work ( );

		void identify ( floppy_drive& drive );

		void move_data ( floppy_drive& drive );

		void format_step ( floppy_drive& drive );

		bool next_record ( );

		void finish ( uint8_t st0, uint8_t st1, uint8_t st2 );

		dma& transfers;
		bus& memory;
		uint8_t* place = nullptr;
		uint32_t wait = 0;
		uint32_t offset = 0;
		uint8_t command [ 9 ] = { };
		uint8_t result [ 7 ] = { };
		uint8_t id [ 4 ] = { };
		uint8_t received = 0;
		uint8_t needed = 0;
		uint8_t result_size = 0;
		uint8_t result_index = 0;
		uint8_t reset_reports = 0;
		uint8_t unit = 0;
		uint8_t head = 0;
		uint8_t last_record = 0;
		uint8_t format_count = 0;
		uint8_t formatted = 0;
		uint8_t field = 0;
		uint8_t filler = 0;
		fdc_task task = TASK_NONE;
		bool multi_track = false;
		bool terminal = false;
		bool stalled = false;
		bool pending = false;
		bool result_clears = false;

	};

}