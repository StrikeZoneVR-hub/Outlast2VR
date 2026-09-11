#pragma once
#include <cmath>
#include <cstdint>
namespace p26 {
struct GripGate {
    bool down=true;
    uint64_t until=0;
    void Reset(){down=true;until=0;}
    bool Update(float grip,bool allowed,uint64_t now){
        if(!std::isfinite(grip)){Reset();return false;}
        if(grip<.35f)down=false;
        if(grip>.65f&&!down){down=true;if(allowed)until=now+100;}
        if(!allowed)until=0;
        return now<until;
    }
};
}
