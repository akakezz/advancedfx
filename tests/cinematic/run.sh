#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
output="$(mktemp -d)"
trap 'rm -rf "$output"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Wpedantic -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
  shared/CinematicPath.cpp shared/CinematicPathXml.cpp \
  tests/cinematic/test_cinematic.cpp -o "$output/cinematic_tests"
"$output/cinematic_tests"
