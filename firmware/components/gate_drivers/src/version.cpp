// version.cpp

#include "gate_drivers/version.hpp"

namespace gate::drivers {

#ifdef GATE_BUILD_SHA
#define GATE_VERSION_STR "0.0.1+" GATE_BUILD_SHA
#else
#define GATE_VERSION_STR "0.0.1"
#endif

const char* const kVersion = GATE_VERSION_STR;

}  // namespace gate::drivers
