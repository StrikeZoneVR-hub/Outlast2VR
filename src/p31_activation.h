#pragma once
#include <cstdint>
namespace p31 {
struct ScanBudget {
    uint64_t started;unsigned visited=0;
    bool Take(uint64_t now){
        if(now<started||now-started>=2||visited>=128)return false;
        ++visited;return true;
    }
};
// Loading and scripted movement cannot arm capture. A fresh key press after
// two seconds of ordinary gameplay is required; held keys never auto-enable it.
struct Activation {
    uintptr_t pawn=0;
    uint64_t stableSince=0;
    bool down=false,enabled=false;
    bool Update(uintptr_t current,bool eligible,bool pressed,uint64_t now){
        if(current!=pawn||!eligible){pawn=current;stableSince=0;enabled=false;down=pressed;return false;}
        if(!stableSince){stableSince=now;down=pressed;return false;}
        const bool edge=pressed&&!down;down=pressed;
        if(edge&&now>=stableSince&&now-stableSince>=2000)enabled=!enabled;
        return enabled;
    }
};
}
