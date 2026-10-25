#pragma once
#include <cstdint>
extern "C" {
int bridge_open(const char* absoluteDll,const char* absoluteRunRoot);
const char* bridge_error();
int bridge_close();
void* bridge_create();
void bridge_delete(void*);
void bridge_mode(void*,int);
int bridge_mode_read(void*);
void bridge_offset(void*,float,float,float,int);
void bridge_process(void*,float,float,float,float,float,float,float);
void bridge_cal(void*,float*,float*,float*);
void bridge_gravity(void*,float*,float*,float*);
void bridge_player(void*,float*,float*,float);
void bridge_world(void*,float*,float*,float);
void bridge_reset(void*);
unsigned bridge_reset_calls();
unsigned bridge_process_calls();
float bridge_confidence(void*);
void bridge_get_offset(void*,float*,float*,float*);
}
