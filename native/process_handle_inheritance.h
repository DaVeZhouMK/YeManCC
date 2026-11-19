#pragma once
#include <mutex>
// One same-process launch lock. Only generic inherited creates and explicit
// temporary inheritance windows use it; no observation, worker or timer.
namespace ymcc::processInheritance {
inline std::recursive_mutex& owner(){static std::recursive_mutex value;return value;}
}
