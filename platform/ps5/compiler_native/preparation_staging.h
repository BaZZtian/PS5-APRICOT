#pragma once
#include <string>
#include <sys/stat.h>
#include <cerrno>
namespace apricot::aot {
inline bool create_preparation_staging(const std::string&destination,std::string&staging){
 for(unsigned attempt=0;attempt<64;++attempt){staging=destination+".preparing-"+std::to_string(attempt);
  if(!mkdir(staging.c_str(),0700))return true;if(errno!=EEXIST)break;
 }staging.clear();return false;
}
}
