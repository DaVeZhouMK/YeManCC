#define NOMINMAX
#include "../native/radial_input_region.h"
#include <cassert>
#include <fstream>
#include <iostream>
int wmain(int argc,wchar_t** argv){assert(argc==2);std::ifstream in(argv[1]);nlohmann::json input;in>>input;unsigned cases=0;
 for(double scale:{.5,.75,1.,1.25,1.5,2.,3.}){auto region=ymcc::radial::makeInputRegion(input["polygons"],scale);assert(region);
  assert(!PtInRegion(region,(int)(248*scale),(int)(248*scale)));++cases;assert(!PtInRegion(region,0,0));++cases;
  for(const auto& p:input["labels"]){assert(PtInRegion(region,(int)std::lround(p[0].get<double>()*scale),(int)std::lround(p[1].get<double>()*scale)));++cases;}
  DeleteObject(region);
 }
 auto malicious=nlohmann::json::parse("[[[0,0],[496,0],[496,560],[0,560]]]");auto region=ymcc::radial::makeInputRegion(malicious,1);assert(!PtInRegion(region,248,248));++cases;DeleteObject(region);
 try{ymcc::radial::makeInputRegion(malicious,0);assert(false);}catch(const std::exception&){++cases;}
 try{ymcc::radial::makeInputRegion(nlohmann::json::parse("[[[-1,0],[1,1],[2,2]]]"),1);assert(false);}catch(const std::exception&){++cases;}
 std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"consumer\":\"native/radial_input_region.h\",\"hardwareValidated\":false}";
}
