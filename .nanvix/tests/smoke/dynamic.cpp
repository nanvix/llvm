// Copyright(c) The Maintainers of Nanvix.
// Licensed under the MIT License.

// Build smoke test: verify that an executable can link against a Nanvix shared
// object and the shared libc++, libc++abi, and libunwind runtimes.

#include <iostream>

extern "C" int nanvix_smoke_shared_value(void);

int main() {
    int value = nanvix_smoke_shared_value();
    std::cout << "shared value = " << value << '\n';

    try {
        if (value != 42)
            throw value;
    } catch (int) {
        return 1;
    }

    return 0;
}
