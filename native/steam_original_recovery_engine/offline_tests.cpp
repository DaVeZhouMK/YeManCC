#include "../steam_original_recovery_engine.h"
#include <iostream>
namespace ymcc::steamOriginalRecovery { Json offline_self_test(const std::string&,const std::string&); }
int main(int argc,char** argv){
  if(argc!=2&&(argc!=4||std::string(argv[2])!="--case"||std::string(argv[3]).empty()))return 64;
  const auto result=ymcc::steamOriginalRecovery::offline_self_test(argv[1],argc==4?argv[3]:"");
  std::cout<<result.dump(2)<<std::endl;return result.at("success")==true?0:2;
}
