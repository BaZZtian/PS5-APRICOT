#pragma once
#include "menu.h"
namespace apricot {
struct MessageBox {int left=0,top=0,width=0,height=0;std::vector<std::string>lines;};
inline MessageBox message_box(const std::string& message,int scale=3){
 MessageBox box;scale=std::clamp(scale,1,16);const int limit=1120;std::string line,word;
 auto append=[&](const std::string& token){for(char c:token){if(!line.empty()&&menuTextWidth(line+c,scale)>limit){box.lines.push_back(line);line.clear();}line+=c;}};
 auto flush=[&]{if(word.empty())return;std::string candidate=line.empty()?word:line+" "+word;if(menuTextWidth(candidate,scale)>limit&&!line.empty()){box.lines.push_back(line);line.clear();}if(!line.empty())line+=' ';append(word);word.clear();};
 for(char c:message.substr(0,4096)){if(c==' '||c=='\n'){flush();if(c=='\n'){box.lines.push_back(line);line.clear();}}else word+=c;}
 flush();if(!line.empty()||box.lines.empty())box.lines.push_back(line);
 if(box.lines.size()>12){box.lines.resize(12);box.lines.back()="...";}
 int widest=0;for(auto& s:box.lines)widest=std::max(widest,menuTextWidth(s,scale));
 box.width=std::clamp(widest+64,280,1184);box.height=std::min(620,std::max(100,int(box.lines.size())*(8*scale+12)+48));
 box.left=(1280-box.width)/2;box.top=(720-box.height)/2;return box;
}
}
