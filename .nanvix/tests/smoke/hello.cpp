// Copyright(c) The Maintainers of Nanvix.
// Licensed under the MIT License.

// Build smoke test: verify that a C++ program compiles and links against
// libc++, libc++abi, libunwind, and compiler-rt for the i686-unknown-nanvix
// target. Exercises new/delete (libc++abi) and std::printf (libc).

#include <cstdio>

int main() {
    int *p = new int(42);
    std::printf("value = %d\n", *p);
    delete p;
    return 0;
}
