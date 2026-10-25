#include "../native/radial_touch_hold.h"
#include <cassert>
#include <iostream>
int main(){using ymcc::radial::PromotedTouchHold;PromotedTouchHold model;constexpr ULONG_PTR touch=0xff515780,pen=0xff515700;
 assert(PromotedTouchHold::touch(touch));assert(!PromotedTouchHold::touch(pen));assert(!PromotedTouchHold::touch(0));
 model.observe(WM_LBUTTONDOWN,touch,{120,50},1000,650);assert(!model.take(1649));assert(model.take(1650));assert(!model.take(1651));
 model.observe(WM_LBUTTONDOWN,touch,{120,50},2000,650);model.observe(WM_LBUTTONUP,touch,{120,50},2100,650);assert(!model.take(3000));
 model.observe(WM_LBUTTONDOWN,touch,{120,50},3000,650);model.observe(WM_MOUSEMOVE,touch,{128,50},3100,650);assert(!model.take(4000));
 model.observe(WM_LBUTTONDOWN,pen,{120,50},5000,650);assert(!model.take(6000));
 model.observe(WM_LBUTTONDOWN,touch,{120,50},7000,650);model.observe(WM_MOUSEMOVE,touch,{123,53},7100,650);assert(model.take(7650));
 model.observe(WM_LBUTTONDOWN,touch,{120,50},8000,650);model.cancel();assert(!model.take(10000));
 std::cout<<"{\"pass\":true,\"cases\":13,\"consumer\":\"native/radial_touch_hold.h\",\"physicalValidated\":false}";
}
