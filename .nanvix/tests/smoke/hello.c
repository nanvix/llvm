/*
 * Copyright(c) The Maintainers of Nanvix.
 * Licensed under the MIT License.
 */

/*
 * Build smoke test: verify that a C program compiles and links against the
 * in-source Nanvix C library and compiler-rt builtins for the
 * i686-unknown-nanvix target.
 */

#include <stdio.h>

int main(void)
{
    printf("value = %d\n", 42);
    return 0;
}
