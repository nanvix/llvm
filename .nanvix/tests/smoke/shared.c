/*
 * Copyright(c) The Maintainers of Nanvix.
 * Licensed under the MIT License.
 */

/*
 * Build smoke test: verify that Clang can emit a shared object for the
 * i686-unknown-nanvix target.
 */

int nanvix_smoke_shared_value(void)
{
    return 42;
}
