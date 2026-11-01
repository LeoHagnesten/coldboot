#pragma once

#include "base/base.hh"

namespace machine {

	class archive;

	struct floppy_format {

		uint32_t size;
		uint8_t cylinders;
		uint8_t heads;
		uint8_t sectors;
		const wchar_t* name;

	};

	class floppy {

	public:

		static constexpr uint32_t sector_size = 512;

		static const floppy_format* identify ( uint32_t size );

		bool load ( const uint8_t* data, uint32_t size );

		void clear ( );

		void transfer ( archive& io );

		bool present ( ) const {

			return format != nullptr;

		}

		bool has_track ( uint8_t cylinder, uint8_t head ) const {

			return format && cylinder < format->cylinders && head < format->heads;

		}

		uint8_t* sector ( uint8_t cylinder, uint8_t head, uint8_t record );

		const floppy_format* format = nullptr;
		base::list < uint8_t > bytes;
		bool protect = false;
		bool changed = false;

	};

}