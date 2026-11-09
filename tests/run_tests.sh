#!/bin/sh
# Build and run the host tests. Results land in tests/results/.
set -e
cd "$(dirname "$0")/.."
mkdir -p tests/results build
clang++ -std=c++17 -O2 -Wall -Wextra -o build/test_vibeguard tests/test_vibeguard.cpp
./build/test_vibeguard tests/results
