# DSU motion reference and transport

The C# oracle receives actual fixed HC BeginPacket, FinishPacket, ReportToBuffer,
SwapYawRoll, InvertAxis, NormalizeXboxInput and time conversion bodies. Stub
controller/button/axis/motion containers contain numeric data only; no HC DLL,
GUI, device factory or hardware is loaded. CRC has an independent bitwise shim
and loopback verification also checks it independently.

Native tests execute exact current producer/matrix/source functions and the
actual dsu_server.cpp. Only physical/Host metadata is memory-fed. The loopback
server uses an ephemeral local port in a separate executable, never the user's
running YMCC, port 26760, a real controller or an external endpoint.
