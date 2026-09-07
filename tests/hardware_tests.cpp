#include "../app/hardware.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
int main() {
    CounterDelta counter;std::uint64_t delta=0;
    assert(!counter.Update(0xfffffff0,32,.25,delta));
    assert(counter.Update(0x20,32,.25,delta) && delta==48);
    assert(!counter.Update(0x30,32,30,delta));
    assert(counter.Update(0x40,32,.25,delta) && delta==16);
    counter.Reset();assert(!counter.Update(0,48,.25,delta));
    AlertFilter alert;
    for(int i=0;i<16;++i)assert(alert.Update(20,.25,90,98)==0);
    assert(alert.Update(100,.25,90,98)==0); // An isolated spike must not flash.
    for(int i=0;i<50;++i)alert.Update(100,.25,90,98);
    assert(alert.level==2);
    assert(alert.Update(0,.25,90,98)==2); // Recovery is also debounced.
    for(int i=0;i<50;++i)alert.Update(0,.25,90,98);
    assert(alert.level==0);
    assert(alert.Update(100,30,90,98)==0);
    assert(!counter.Update(1,0,.25,delta));
    const auto topology=DetectTopology();assert(!topology.logical.empty());
    std::vector<int> seen(topology.logical.size());
    for(std::size_t physical=0;physical<topology.cores.size();++physical) {
        for(auto lp:topology.cores[physical].siblings){assert(lp<seen.size());++seen[lp];assert(topology.logical[lp].physical==physical);assert(topology.logical[lp].package==topology.cores[physical].package);}
    }
    for(int count:seen)assert(count==1);
    std::printf("PASS: counter wrap, suspend rebaseline, alert spike/recovery, topology coverage and SMT mapping (%zu cores / %zu LPs)\n",topology.cores.size(),topology.logical.size());
}
