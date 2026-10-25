#include "fan_http_body_contract.h"
#include <iostream>
#include <stdexcept>
struct Case{const char* name;const char* method;unsigned long status;const char* headers;size_t bytes;bool expected;};
int main(){const Case cases[]={
 {"exact","POST",200,"HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\n",7,true},
 {"short","POST",200,"Content-Length: 7\r\n",6,false},
 {"long","POST",200,"Content-Length: 7\r\n",8,false},
 {"case and whitespace","POST",200,"cOnTeNt-LeNgTh:\t7 \r\n",7,true},
 {"same duplicate","POST",200,"Content-Length: 7\r\nContent-Length: 7\r\n",7,true},
 {"different duplicate","POST",200,"Content-Length: 7\r\nContent-Length: 8\r\n",7,false},
 {"empty length","POST",200,"Content-Length:\r\n",0,false},
 {"signed length","POST",200,"Content-Length: +7\r\n",7,false},
 {"negative length","POST",200,"Content-Length: -7\r\n",7,false},
 {"decimal length","POST",200,"Content-Length: 7.0\r\n",7,false},
 {"overflow","POST",200,"Content-Length: 18446744073709551616\r\n",0,false},
 {"64bit no truncation","POST",200,"Content-Length: 4294967303\r\n",7,false},
 {"zero","POST",200,"Content-Length: 0\r\n",0,true},
 {"missing length","POST",200,"HTTP/1.1 200 OK\r\nConnection: close\r\n",7,true},
 {"chunked","POST",200,"Transfer-Encoding: chunked\r\n",7,true},
 {"ambiguous framing","POST",200,"Transfer-Encoding: chunked\r\nContent-Length: 7\r\n",7,false},
 {"head declared entity length","HEAD",200,"Content-Length: 99\r\n",0,true},
 {"head lower case","head",200,"Content-Length: 99\r\n",0,true},
 {"head unexpected bytes","HEAD",200,"Content-Length: 99\r\n",7,false},
 {"no content","POST",204,"Content-Length: 99\r\n",0,true},
 {"not modified","GET",304,"Content-Length: 99\r\n",0,true},
 {"informational","GET",101,"Content-Length: 99\r\n",0,true},
};int count=0;for(const auto& c:cases){if(ymcc::fan_http::responseBodyComplete(c.method,c.status,c.headers,c.bytes)!=c.expected)throw std::runtime_error(c.name);count++;}std::cout<<"PASS "<<count<<" Fan-only body header cases; pure bytes, no HTTP/device/CPU benchmark\n";}
