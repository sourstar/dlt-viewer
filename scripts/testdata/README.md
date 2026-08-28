# Synthetic DLT test data and benchmarks

Tools for generating DLT log files and measuring the parsing hot paths without
needing real (and usually confidential) vehicle logs.

## dltgen.c — generate a test file

```sh
cc -O2 -o dltgen dltgen.c
./dltgen bench.dlt 500          # 500 MiB, ~5 million messages
```

Produces DLTv1 storage records with verbose log messages spread over 8
application ids, 8 context ids and a realistic mix of log levels. Every 101st
message contains the token `NEEDLE`, which gives filters and searches something
with a known hit count (`size / 101` messages).

## dltbench.cpp — measure the hot paths

Links against `qdlt` and times the three passes that dominate opening and
filtering a file, with no GUI involved:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDLT_PARSER=OFF
cmake --build build --target qdlt
g++ -O2 -std=c++17 -fPIC scripts/testdata/dltbench.cpp -o dltbench \
    -Iqdlt -Isrc -Ibuild/qdlt $(pkg-config --cflags Qt6Core) \
    -Lbuild/bin -lqdlt $(pkg-config --libs Qt6Core)
LD_LIBRARY_PATH=build/bin ./dltbench bench.dlt
```

## Reference numbers

500 MiB / 5,052,715 messages, Ryzen 7 9700X, GCC 14 `-O2`, warm page cache.
Both revisions matched the same 50,027 messages.

| Pass | upstream `edf7aa5` | this fork | |
|---|---|---|---|
| build message index | 7700 ms | 1552 ms | 5.0x |
| read + parse every message | 6898 ms | 2350 ms | 2.9x |
| filter pass (payload match) | 7884 ms | 3010 ms | 2.6x |

Open plus filter end to end: 15.6 s to 4.6 s, about 3.4x.
