#pragma once

#include "hv/state.hh"

namespace hv {

	enum emulate_result : uint8_t {

		EMULATE_DONE,
		EMULATE_FAULT,
		EMULATE_UNSUPPORTED,

	};

	emulate_result emulate ( cpu_state& cpu, guest_bus& bus, const uint8_t* bytes, uint32_t count );

}