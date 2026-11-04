# Implementation validation

Base: `3906a61e493038fdf7897e8366c225148ade1a56`.
Branch: `feature/cinematic-campath`.

Executed in the Linux implementation workspace:

- GCC 14, C++17, AddressSanitizer and UndefinedBehaviorSanitizer: **795 checks
  passed**. Command: `ASAN_OPTIONS=detect_leaks=0 tests/cinematic/run.sh`.
  LeakSanitizer cannot run under this host's tracing/sandbox; leak detection was
  disabled for that reason. Address and undefined-behavior checks stayed enabled.
- Standalone CMake 3.31.6 Debug build and CTest: **1/1 test passed**, executing the
  same core suite. No dependency on the rest of HLAE is required for that target.
- Exact core/test sources cross-compiled and linked as Windows x86 and x64
  executables using Zig 0.13's LLVM/MinGW toolchain. They were **not executed**.
- New Windows editor and drawer sources compiled to x86 and x64 COFF objects
  against the repository-pinned Source SDK headers and Windows/Direct3D headers.
  This check used a temporary external copy of `WrpConsole.h` with its four
  MSVC-qualified callback definitions written as equivalent overloads. It also
  removed the MSVC `abstract` decoration via a compiler define and enabled delayed
  template parsing. No upstream header in the branch was changed for this check.
  This is a cross-compiler integration check, not a full MSVC/HLAE build or link.
- `git diff --check`: passed.

The portable checks cover timing budgets, stops/holds, explicit segment timing,
custom speed/progress, reversing playback, stationary turns, shortest-arc rotation,
curve evaluation/arc-length speed, exact circles, anchors, snapping, XML round
trips/rejections, classic import/export, transactional edits, bounded undo/redo,
selection/dirty state, overlap resolution, transitions and nonsequential seeking.

Full Visual Studio builds, actual DLL injection, Source keyboard routing, demo
camera handoff, recording, graphics state restoration and device resets remain
Windows/in-game validation work. Follow `REVIEW.md`. The command guide explicitly
records linear legacy import, sampled legacy export, approximate spline arc-length,
ASCII visualization labels, pose-blend transitions and scope (Source 1 only).
