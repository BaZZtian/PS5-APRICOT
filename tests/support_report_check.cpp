// SPDX-License-Identifier: MIT
#include "apricot/support_report.hpp"
#include <limits>
int main(int argc,char**argv){
    if(argc!=2)return 1;
    apricot::SupportReport r;
    if(r.set_game(nullptr)||r.set_game(reinterpret_cast<const uint8_t*>("BAD!")))return 2;
    if(!r.set_game(reinterpret_cast<const uint8_t*>("IPKD")))return 3;
    r.phase=apricot::SupportReport::Phase::Stopped;
    r.frames=std::numeric_limits<uint64_t>::max();r.stop9=3;r.stop7=1;
    char b[1024];const auto n=r.render(b,sizeof(b));
    if(!n||n>=sizeof(b)||r.render(b,4)||r.render(nullptr,0))return 4;
    if(!r.write_atomic(argv[1])||!r.write_atomic(argv[1]))return 5;
    r.stop9=7;if(r.write_atomic(argv[1]))return 6;r.stop9=3;
    r.phase=apricot::SupportReport::Phase(999);if(r.write_atomic(argv[1]))return 7;
    r.phase=apricot::SupportReport::Phase::Exit;r.game_code[4]='x';if(r.write_atomic(argv[1]))return 8;
}
