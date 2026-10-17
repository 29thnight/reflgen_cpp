# Standard backend verification

Use a compiler implementing reflection, typed annotations and static strings (validated with GCC 16.2).
Use a recent CMake with C++26 and module-scanning support (validated with CMake 4.2.3).

```sh
cmake -G Ninja -S tests/standard -B build/standard -DCMAKE_CXX_COMPILER=g++
cmake --build build/standard
ctest --test-dir build/standard --output-on-failure
```

The four tests cover header schemas, legacy opt-out, exported module types/user annotations and
`import reflgen.json`. Runtime checks include registration, JSON/binary round trips, method invocation,
ignored fields, transient fields and exception paths. Compile-time checks cover enum values outside the
legacy scan range, private exclusion, annotation values, parameter names and manual-schema precedence.

Install the package, then repeat with `-DREFLGEN_STANDARD_INSTALLED=ON -DCMAKE_PREFIX_PATH=<prefix>` to test
the installed helper, public headers and module sources.

The independent `selection` fixture also runs on compilers without standard reflection:

```sh
cmake -G Ninja -S tests/standard/selection -B build/selection -DTEST_STANDARD=26 -DTEST_BACKEND=AUTO -DTEST_EXPECTED=LEGACY
cmake --build build/selection
ctest --test-dir build/selection --output-on-failure
```

Set `TEST_EXPECTED=STD` on a supporting compiler. `TEST_BACKEND=STD` must fail configuration on an
unsupported compiler. `TEST_STANDARD=20 TEST_BACKEND=AUTO` must choose LEGACY even on GCC 16.

Run all language/backend combinations with the same CMake that knows the selected compiler:

```sh
cmake -DCOMPILER=/path/to/g++ -DWORK=/tmp/reflgen-matrix -DSUPPORTS_STD=ON -P tests/standard/check_matrix.cmake
```

For MSVC, run in the VS developer shell, set `COMPILER` to cl.exe's full path and `SUPPORTS_STD=OFF` while
reflection is unavailable. The script builds/runs AUTO and LEGACY in C++20/23/26, tests forced STD,
rejects forced header activation in C++20/23, and verifies explicit opt-out even with reflection available.
`matrix-results.txt` records the actual language and feature macros; `matrix-build.log` retains compiler output.
MSVC's C++23 preview and C++26 latest modes are compile-probed so older CMake does not silently decay C++26.
