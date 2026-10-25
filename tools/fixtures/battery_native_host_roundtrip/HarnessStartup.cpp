#include <windows.h>
#include <exception>
struct OwnedHarnessStartup{
 OwnedHarnessStartup(){std::set_terminate([]{const char* message="unexpected native exception";try{if(auto ex=std::current_exception())std::rethrow_exception(ex);}catch(const std::exception& e){message=e.what();std::cerr<<"ORIGINAL_SELFTEST_ASSERTION: "<<message<<std::endl;memory::finish(message);std::_Exit(23);}catch(...){}memory::finish(message);std::_Exit(23);});}
 ~OwnedHarnessStartup(){memory::finish();}
} ownedHarnessStartup;
