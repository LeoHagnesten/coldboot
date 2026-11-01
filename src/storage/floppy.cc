#include "storage/floppy.hh"
#include "machine/archive.hh"

namespace machine {

	static const floppy_format formats [ ] = {

		{ 163840, 40, 1, 8, L"160 KB" },
		{ 184320, 40, 1, 9, L"180 KB" },
		{ 327680, 40, 2, 8, L"320 KB" },
		{ 368640, 40, 2, 9, L"360 KB" },
		{ 737280, 80, 2, 9, L"720 KB" },
		{ 1228800, 80, 2, 15, L"1.2 MB" },
		{ 1474560, 80, 2, 18, L"1.44 MB" },

	};

	const floppy_format* floppy::identify ( uint32_t size ) {

		for ( const floppy_format& entry : formats ) {

			if ( entry.size == size )
				return &entry;

		}

		return nullptr;

	}

	bool floppy::load ( const uint8_t* data, uint32_t size ) {

		const floppy_format* found = identify ( size );

		if ( !found || !data )
			return false;

		bytes.resize ( size );

		for ( uint32_t i = 0; i < size; i++ )
			bytes [ i ] = data [ i ];

		format = found;
		changed = false;

		return true;

	}

	void floppy::transfer ( archive& io ) {

		uint32_t size = format ? format->size : 0;
		bool locked = protect;
		bool dirty = changed;

		io.field ( size );
		io.field ( locked );
		io.field ( dirty );

		if ( io.writing ( ) ) {

			if ( size )
				io.block ( bytes.data ( ), size );

			return;

		}

		clear ( );

		if ( !size )
			return;

		const floppy_format* found = identify ( size );

		if ( !found ) {

			io.failed = true;

			return;

		}

		bytes.resize ( size );
		io.block ( bytes.data ( ), size );
		format = found;
		protect = locked;
		changed = dirty;

	}

	void floppy::clear ( ) {

		bytes.clear ( );
		format = nullptr;
		protect = false;
		changed = false;

	}

	uint8_t* floppy::sector ( uint8_t cylinder, uint8_t head, uint8_t record ) {

		if ( !format || cylinder >= format->cylinders || head >= format->heads || record < 1 || record > format->sectors )
			return nullptr;

		uint32_t index = ( static_cast < uint32_t > ( cylinder ) * format->heads + head ) * format->sectors + record - 1u;

		return bytes.data ( ) + index * sector_size;

	}

}