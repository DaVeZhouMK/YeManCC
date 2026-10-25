#pragma once
#include <stdexcept>
#include <windows.h>
namespace ymcc::processEfficiency {template<class...T>bool createProcess(T...){++memory::launchAttempts;throw std::runtime_error("Production Host launch refused by owned memory boundary");}}
