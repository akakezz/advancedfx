"""Reuse the sequencer's demo fixtures; reject expected first-chance C++ throws.
No Source SDK, RapidXML or Windows dependencies are required for this parser test.
"""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
source = (root / 'tests/cinematic/test_sequencer.cpp').read_text()
fixture = source[source.index('struct Writer'):source.index('} // namespace\nint main(')]
prelude = r'''
#include "shared/DemoTimeline.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
void Check(bool ok, const char *message) { if (!ok) { std::cerr << message << '\n'; std::abort(); } }
// Linker interception catches even exceptions which the parser would catch.
extern "C" [[noreturn]] void __wrap___cxa_throw(void *, void *, void (*)(void *)) {
    std::cerr << "Unexpected C++ throw during demo parsing\n"; std::abort();
}
'''
main = r'''
int main(int argc, char **argv) {
    std::filesystem::path dir(argv[1]);
    Indexing(dir);
    std::atomic_bool cancel{false}; std::atomic<float> progress{0};
    auto path = dir / "malformed.dem";
    auto run = [&](const auto &bytes) {
        std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()); out.close();
        auto result = advancedfx::DemoTimeline::Read(path,cancel,progress);
        Check(result.Error.empty() || result.Events.empty(), "Failed index must never expose partial events");
        return result;
    };
    // Every truncated prefix, including compression payloads and table definitions.
    unsigned tested = 0;
    for (unsigned compression : {0u,1u,2u}) {
        auto valid = Demo(compression);
        for (size_t n=0; n<valid.size()-4; ++n) {
            auto result = run(std::vector<unsigned char>(valid.begin(),valid.begin()+n));
            Check(!result.Error.empty(), "Truncated demo accepted"); ++tested;
        }
        std::mt19937 random(20261124+compression);
        for (unsigned i=0; i<1000; ++i) {
            auto mutated=valid;
            auto pos=1072+random()%(mutated.size()-1072);
            mutated[pos]^=static_cast<unsigned char>(1u<<(random()%8));
            run(mutated); ++tested;
        }
    }
    auto header = Demo(); header.resize(1072);
    Writer oversized; oversized.U(32,6); oversized.U(0xffffffffu,32);
    Packet(header,2,10,oversized.Data); header.push_back(7);
    auto malformed=run(header);
    Check(!malformed.Error.empty(),"Overflowing bit length accepted");
    Check(malformed.Error.find("network protocol")!=std::string::npos && malformed.Error.find("message 32")!=std::string::npos && malformed.Error.find("tick 10")!=std::string::npos,"Failure should identify packet protocol, type and tick");
    auto unknown=Demo(); unknown[12]=99; unknown[13]=unknown[14]=unknown[15]=0;
    Check(run(unknown).Error.find("network protocol 99")!=std::string::npos,"Unsupported protocol must identify its actual value");
    // Source 2013 can cap the snapshot after a complete userinfo table.
    auto capped = [&](bool userFirst, bool of, size_t length) {
        auto bytes=Demo(1);
        if(of) memcpy(bytes.data()+796,"open_fortress",13);
        Writer snapshot; snapshot.U(2,8);
        if(userFirst) {
            snapshot.String("userinfo");snapshot.U(1,16);snapshot.String("0");snapshot.U(1,1);
            snapshot.U(36,16);snapshot.Bytes(User("Recorder",1));snapshot.U(0,1);
        }
        snapshot.String("Scenes");snapshot.U(65535,16);
        while(snapshot.Data.size()<=length){snapshot.String(std::string(200,'x'));snapshot.U(0,1);}
        snapshot.Data.resize(length);
        std::vector<unsigned char> command;Packet(command,8,0,snapshot.Data);
        size_t next=1072+93;
        unsigned payload=0;memcpy(&payload,bytes.data()+1072+89,4);next+=payload;
        bytes.insert(bytes.begin()+next,command.begin(),command.end());return run(bytes);
    };
    auto recovered=capped(true,true,512*1024);
    Check(recovered.Error.empty()&&!recovered.Warning.empty()&&recovered.Events.size()==5&&recovered.Events[0].RecorderDeath,"Complete OF userinfo survives capped unrelated tables");
    Check(!capped(false,true,512*1024).Error.empty(),"Capped snapshot without complete userinfo rejects");
    Check(!capped(true,false,512*1024).Error.empty(),"Other games retain strict snapshot validation");
    Check(!capped(true,true,512*1024-1).Error.empty(),"Other lengths retain strict snapshot validation");
    if(argc>2){
        auto actual=advancedfx::DemoTimeline::Read(argv[2],cancel,progress);
        Check(actual.Error.empty()&&progress==1,"Actual OF demo indexes through end");
        Check(actual.TotalTicks==79720&&actual.Events.size()==100&&actual.Events.front().Tick==339&&actual.Events.front().RecorderDeath,"Actual OF fixture matches verified event count and identity");
        Check(!actual.Warning.empty(),"Capped snapshot warning visible");
        std::cout<<"Actual OF demo: "<<actual.Events.size()<<" events, "<<actual.TotalTicks<<" ticks, "<<actual.Warning<<"\n";
    }
    auto missing=advancedfx::DemoTimeline::Read(dir/"missing.dem",cancel,progress);
    Check(!missing.Error.empty() && missing.Events.empty(),"Missing file should be an ordinary error");
    std::cout << "Valid demos, cancellation, missing file, and " << tested << " malformed/truncated inputs checked without a C++ throw\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    cpp=tmp/'demo.cpp'; cpp.write_text(prelude+fixture+main)
    binary=tmp/'demo'
    cmd=['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I'+str(root),str(cpp),str(root/'shared/DemoTimeline.cpp'),'-Wl,--wrap=__cxa_throw','-o',str(binary)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(binary),str(tmp),*sys.argv[1:]],check=True,timeout=90)
