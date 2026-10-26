#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p build/tests
"${CC:-cc}" -std=gnu11 -g -O1 -DLC_DEBUG -DHAS_SOCKLEN_T=1 -DHAVE_CLOCK_GETTIME=1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections -I src -I enet/include \
    tests/test_pyrowave_depacketizer.c src/ByteBuffer.c src/LinkedBlockingQueue.c \
    src/Platform.c src/Misc.c -Wl,--gc-sections -pthread \
    -o build/tests/pyrowave-depacketizer
build/tests/pyrowave-depacketizer
