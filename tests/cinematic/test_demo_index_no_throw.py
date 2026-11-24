"""Reuse the sequencer's demo fixtures; reject expected first-chance C++ throws.
No Source SDK, RapidXML or Windows dependencies are required for this parser test.
"""
from pathlib import Path
import subprocess
import tempfile

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
    Check(!run(header).Error.empty(),"Overflowing bit length accepted");
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
    subprocess.run([str(binary),str(tmp)],check=True,timeout=90)
