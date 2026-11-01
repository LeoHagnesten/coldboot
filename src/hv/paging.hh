#pragma once

#include "hv/state.hh"

namespace hv {

	bool translate ( cpu_state& cpu, guest_bus& bus, uint64_t linear, bool write, uint64_t& physical );

}