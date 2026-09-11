#include "../src/p27_native_hud.h"
#include <array>
#include <cstdio>
int main(){
    std::array<uint8_t,sizeof(p27::RaiseBytes)> bytes{};
    std::memcpy(bytes.data(),p27::RaiseBytes,bytes.size());
    if(!p27::MatchesRaise(bytes.data(),bytes.size()))return 1;
    if(p27::MatchesRaise(nullptr,bytes.size())||p27::MatchesRaise(bytes.data(),bytes.size()-1))return 2;
    for(std::size_t i=0;i<bytes.size();++i){
        bytes[i]^=1;if(p27::MatchesRaise(bytes.data(),bytes.size()))return 3;bytes[i]^=1;
    }
    bytes[p27::ModeByte]=2;
    if(p27::MatchesRaise(bytes.data(),bytes.size()))return 4;
    for(std::size_t i=0;i<bytes.size();++i)if(i!=p27::ModeByte&&bytes[i]!=p27::RaiseBytes[i])return 5;
    std::puts("P27 exact signature, truncation/null rejection, all-byte mismatch and one-byte change tests passed.");
}
