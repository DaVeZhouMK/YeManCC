#pragma once
namespace ymcc {
// No foreground requirement. Hidden/iconic windows must still be summonable,
// even if Windows remembers their last maximized/full-height placement.
inline bool summonPressHides(bool valid, bool visible, bool iconic, bool zoomed, bool fullHeight) {
    return valid && visible && !iconic && (zoomed || fullHeight);
}
}
