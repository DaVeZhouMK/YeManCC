#pragma once
#include "game_feedback_receipt.h"
#include "input_frame_submission.h"
#include "touch_haptic_dispatch.h"
#include <memory>
namespace ymcc::inputHost::sharedPower {class Scope;}
namespace ymcc::game_feedback {
// Captured source and its existing input permission. Copies retain that same
// permission without creating a new lease or performing I/O.
struct SourceBoundCommand {
    input::frameSubmission::Ticket ticket{};
    input::frameSubmission::Sample read{};
    touchhaptic::SourceToken source{};
    Sample motors{};
    bool valid=false;
    std::shared_ptr<const inputHost::sharedPower::Scope> powerScope;
};
}
