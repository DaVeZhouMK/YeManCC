// Never execute this binary. Its real DeviceIoControl import must be rejected before launch.
#include <windows.h>
int main(){DWORD returned=0;return DeviceIoControl(INVALID_HANDLE_VALUE,0,nullptr,0,nullptr,0,&returned,nullptr)?0:1;}
