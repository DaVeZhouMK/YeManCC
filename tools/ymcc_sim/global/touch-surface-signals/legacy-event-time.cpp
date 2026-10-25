#define NOMINMAX
#include <windows.h>
// Only inline bitmap singleton destructors reference these. No allocation occurs.
inline unsigned unusedBitmapCleanupCalls=0;
inline BOOL memoryDeleteDC(HDC){++unusedBitmapCleanupCalls;return TRUE;}
inline BOOL memoryDeleteObject(HGDIOBJ){++unusedBitmapCleanupCalls;return TRUE;}
inline HGDIOBJ memorySelectObject(HDC,HGDIOBJ){++unusedBitmapCleanupCalls;return nullptr;}
#define DeleteDC memoryDeleteDC
#define DeleteObject memoryDeleteObject
#define SelectObject memorySelectObject
#include "tools/screen_touchpads_event_time_selftest.cpp"
