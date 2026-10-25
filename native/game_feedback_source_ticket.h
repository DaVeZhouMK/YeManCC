#pragma once
#include "game_feedback_receipt.h"
#include "input_frame_submission.h"
#include "touch_haptic_dispatch.h"
namespace ymcc::game_feedback {
// Fixed POD held by the native caller; receipt support alone grants no I/O authority.
struct SourceBoundCommand {
    input::frameSubmission::Ticket ticket{};
    input::frameSubmission::Sample read{};
    touchhaptic::SourceToken source{};
    Sample motors{};
    bool valid=false;
};
}
