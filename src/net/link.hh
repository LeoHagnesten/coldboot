#pragma once

#include "base/base.hh"

namespace net {

	struct port {

		virtual ~port ( ) = default;

		virtual void receive ( const uint8_t* frame, uint32_t size ) = 0;

	};

	struct wire {

		virtual ~wire ( ) = default;

		virtual void send ( const uint8_t* frame, uint32_t size ) = 0;

	};

	uint32_t sum_words ( const uint8_t* data, uint32_t size, uint32_t sum );

	uint16_t fold ( uint32_t sum );

	uint32_t crc32 ( const uint8_t* data, uint32_t size );

}