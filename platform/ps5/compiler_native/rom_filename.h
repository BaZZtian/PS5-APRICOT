#pragma once
#include <string>
namespace apricot::aot {
inline bool is_rom_filename(const std::string&name){
 if(name.size()<=4||name.size()>255||name.find_first_of("/\\")!=std::string::npos||name.find('\0')!=std::string::npos)return false;
 const auto n=name.size();return name[n-4]=='.'&&(name[n-3]=='n'||name[n-3]=='N')&&(name[n-2]=='d'||name[n-2]=='D')&&(name[n-1]=='s'||name[n-1]=='S');
}
}
