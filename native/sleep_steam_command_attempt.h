#pragma once
#include "steam_command_disposition.h"
#include <functional>
namespace ymcc::sleepsteam {
// Stack-owned original caller accounting. Only the local transport writes disposition.
// admit is invoked through its existing native BEFORE-send callback.
struct CommandAttempt {
    steamcommand::Disposition disposition=steamcommand::Disposition::Unavailable;
    std::function<bool()> admit;
};
}
