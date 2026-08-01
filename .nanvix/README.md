# Nanvix LLVM Port

This directory holds the Nanvix-specific assets for the LLVM toolchain port.
This document explains how the port is structured, how the pieces fit together,
and how the toolchain is built.

## Overview

The port turns upstream LLVM into cross-toolchains that produce static ELF
executables for **Nanvix**, a Unix-like research operating system. The supported
target triples are:

```
i686-unknown-nanvix
x86_64-unknown-nanvix
```

The port has two kinds of changes. Most of the work lives in a small set of
**new, Nanvix-specific assets**; the rest is a handful of **minimal edits to
upstream LLVM/Clang** that register the operating system so those assets are
reachable.

The new Nanvix-specific assets are:

1. A **Clang driver toolchain** that knows how to compile, assemble, and link
   for Nanvix (`clang/lib/Driver/ToolChains/Nanvix.{h,cpp}`).
2. A **CMake platform module** that lets CMake cross-configure the LLVM
   runtimes for Nanvix (`cmake/nanvix/Platform/Nanvix.cmake`).
3. A **build orchestrator**, the `z` script at the repository root, that drives
   the whole two-stage build and stages the Nanvix sysroot.
4. **Supporting infrastructure**: a `Dockerfile` for the released toolchain
   image, GitHub Actions workflows (`.github/workflows/nanvix-*.yml`), and the
   build-only smoke tests under `.nanvix/tests/`.

The upstream integration points (kept as small as possible) are:

- **Triple registration** — `llvm/lib/TargetParser/Triple.{h,cpp}` add the
  `Nanvix` OS type and map the `nanvix` triple component to it.
- **Clang target info** — `clang/lib/Basic/Targets.cpp`, `Targets/OSTargets.h`,
  and `Targets/X86.h` add the Nanvix targets (defining
  `__nanvix__`/`__unix__` and the x86-32/x86-64 type layouts).
- **Clang driver wiring** — `clang/lib/Driver/Driver.cpp` dispatches the triple
  to the toolchain, `Driver/CMakeLists.txt` builds it, and
  `Driver/ToolChains/CommonArgs.cpp` and `Lex/InitHeaderSearch.cpp` special-case
  the OS. `clang/test/Driver/nanvix.c` covers the driver.
- **Runtime source patches** — small `__nanvix__` guards in `compiler-rt`
  (`builtins/cpu_model/x86.c`, `sanitizer_common/sanitizer_platform.h`) and
  `libc++` (`include/__config`, `src/chrono.cpp`, `src/random.cpp`) adapt the
  runtimes to the Nanvix libc.

The build is intentionally split into two stages so the compiler is produced
first, then used to cross-compile its own runtime libraries.

## Build stages

The `z` script (see `./z help`) builds the toolchain in two stages:

### Stage 0 — the compiler

Builds Clang and LLD only, with `X86` as the sole backend and the default triple
selected by `./z configure --target` (`i686-nanvix` by default, or
`x86_64-nanvix`). No runtimes and no sysroot are required. The result defaults
to the selected Nanvix target while remaining able to compile for either triple
explicitly.

### Stage 1 — the runtimes

Uses the freshly built stage 0 Clang to cross-compile the LLVM runtimes for the
same Nanvix target:

- `compiler-rt` (builtins only — baremetal build, no sanitizers/profile/etc.)
- `libunwind` (static and shared)
- `libc++abi` (static and shared)
- `libc++` (static and shared)

Executable links continue to use the static variants by default. The shared
variants are available for libraries and plugins loaded with `dlopen`.

Before configuring, stage 1 stages the **Nanvix C library** into the sysroot:
the in-source headers (`include/`), the C/math libraries (`libc.a`, `libm.a`,
`libc.so`, `libm.so`), the startup objects (`crt0.o`, `libnvx_crt0.a`), the
empty stub archives (`libdl.a`, `libpthread.a`, `librt.a`), and the user linker
script (`user.ld`). The libc source is either a local Nanvix checkout
(`Z_NANVIX_SRC`) or a matching Nanvix release tarball
(`Z_NANVIX_RELEASE_TAG`). The script validates `crt0.o`'s ELF class before
staging it, so a 32-bit libc cannot silently enter an x86_64 sysroot (or vice
versa). If the pinned release does not publish an x86_64 archive, build its
source checkout first with:

```bash
make -C "$Z_NANVIX_SRC" nanvix-libc-bundle \
  TARGET=x86_64 RELEASE=yes LOG_LEVEL=error
```

The in-source Nanvix libc has grown enough C and POSIX coverage that the stage 1
libc++ is now built with its full default feature set — wide characters,
localization, and `<filesystem>` are all enabled.

## The Clang driver toolchain

`clang/lib/Driver/ToolChains/Nanvix.cpp` implements the `Nanvix` toolchain
(derived from `Generic_ELF`) and a custom linker job. Key behaviors:

- **Defaults**: LLD is the linker, compiler-rt is the runtime library, libc++
  is the C++ standard library, and `math-errno` is off.
- **x86_64 ABI**: the red zone is disabled by default because Nanvix exception
  and signal delivery do not preserve it. Explicit `-mred-zone` still wins.
- **Sysroot search**: both `$SYSROOT/lib` and `$SYSROOT/usr/lib` are searched so
  `crt0.o` and the archives resolve regardless of staging prefix. System C
  headers come from `$SYSROOT/usr/include`; libc++ headers come from
  `<install>/include/c++/v1`.
- **Static ELF links**: the linker job forces `-Bstatic`, disables
  `--build-id`, `--rosegment`, and `RELRO`, and enables `--eh-frame-hdr` and
  `--gc-sections`. These constraints come from the Nanvix ELF loader, which
  requires page-aligned `LOAD` segments and cannot handle a `GNU_RELRO`
  segment.
- **Startup**: Nanvix ships a single startfile, `crt0.o` (entry `_do_start`);
  there is no `crti`/`crtbegin`/`crtend`/`crtn`. Constructors/destructors run
  via `.preinit_array`/`.init_array`/`.fini_array`, whose bounds are provided by
  the user linker script.
- **Standard libraries**: linked inside a `--start-group ... --end-group` to
  resolve the circular dependencies between the static archives, pulling in the
  C++ runtime (`-lc++ -lc++abi -lunwind`), compiler-rt builtins, `-lm`, and
  `-lc`.
- **Shared objects**: `-shared` links emit no ELF interpreter (Nanvix has no
  dynamic loader; `.so`s are brought in via `dlopen`) and allow text
  relocations. Nanvix supplies PIC-built shared libc artifacts for x86_64.
- **ELF64 self-linking**: x86_64 executable links use
  `--apply-dynamic-relocs`, preserving ELF64 RELA entries while materializing
  their link-time addends for Nanvix startup self-linking.

## The CMake platform module

`cmake/nanvix/Platform/Nanvix.cmake` teaches CMake that `Nanvix` is a UNIX-like
ELF platform. Without it, configuring the stage 1 runtimes fails because CMake
cannot classify `CMAKE_SYSTEM_NAME=Nanvix`. The module marks the platform as
`UNIX`, clears `CMAKE_DL_LIBS` (libdl is a stub), and sets the ELF
shared-library conventions used when the `.so` runtime variants are built. The
`z` stage 1 configure step adds this directory to `CMAKE_MODULE_PATH`.

## Sysroot layout

After a stage 1 build, the sysroot looks like:

```
$SYSROOT/
├── usr/include/     # Nanvix in-source C headers
└── lib/             # crt0.o, libnvx_crt0.a, libc.a, libm.a, libc.so,
                     # libm.so, libdl.a, libpthread.a, librt.a, user.ld
```

and the install prefix gains the LLVM runtimes:

```
$INSTALL/
├── bin/             # clang, clang++, lld, ...
└── lib/
    ├── libc++.{a,so*}, libc++abi.{a,so*}, libunwind.{a,so*}
  └── clang/<ver>/lib/<triple>/libclang_rt.builtins*.a
```

## Tests

`.nanvix/tests/smoke/` contains build-only smoke tests exercised by `./z test`:

- `hello.c` — a minimal C program (libc + compiler-rt).
- `hello.cpp` — a minimal C++ program exercising `new`/`delete` (libc++abi) and
  `std::printf` (libc), linked against libc++, libc++abi, libunwind, and
  compiler-rt.
- `shared.c` — a position-independent C library linked as `libhello.so`.
- `dynamic.cpp` — a C++ executable linked against `libhello.so` and the shared
  libc++, libc++abi, and libunwind runtimes.

`./z test` compiles and links all four for the configured target using the
installed toolchain (it does not run them). The dynamic test names the
versioned runtime shared objects explicitly and brackets them with
`-Bdynamic`/`-Bstatic` so the driver cannot fall back to archives while the
default system libraries remain static. The executable uses Nanvix's
self-linking flags, including SysV hashing and no `PT_INTERP`; the x86_64 link
also materializes ELF64 RELA addends. `./z verify` checks the configured
target's compiler-rt directory and the expected static/shared runtime artifacts.

## Building the toolchain

From the repository root, choose `i686-nanvix` or `x86_64-nanvix` and use the
same target for both stages:

```bash
TARGET=x86_64-nanvix

./z setup                       # install host build dependencies (Ubuntu)
./z configure --target="$TARGET" --stage=0
./z build                       # build Clang + LLD
./z install                     # install stage 0
./z configure --target="$TARGET" --stage=1
./z build                       # build the runtimes
./z install                     # install stage 1
./z verify                      # check installed artifacts
./z test                        # build the smoke tests
```

See `./z help` for the full list of commands and options.

## Known Limitations

The port is constrained by what the in-source Nanvix C library and OS currently
provide. Each limitation below is a workaround that should be **removed or
reverted once Nanvix ships the required functionality**; each links the
`nanvix/nanvix` issue that tracks the blocking OS work. The controlling settings
live in the `z` script (stage 1 configure) and in the Clang driver.

The **Status** notes record what was verified against a Nanvix source tree on
2026-07-02 by staging the libc and building the stage 1 runtimes with each
feature re-enabled. They are a moving target — re-verify against the Nanvix
release you are building with.

- **`librt` is incomplete (empty stub archives)**
  (`libdl.a`, `libpthread.a`, `librt.a`).
  These are empty `ar` archives staged only so `-ldl`/`-lpthread`/`-lrt`
  resolve; the driver also passes `--no-dependent-libraries` so the `pthread`/
  `rt` dependency metadata embedded by libc++/libc++abi is ignored. `dl` and
  `pthread` are fully functional — their symbols are consolidated into `libc.a`
  (the relibc-style design), so nothing more is needed there. **`librt` is the
  real gap**: the POSIX realtime APIs (`shm_open`, `timer_create`, `mq_*`,
  `aio_*`, named semaphores, `clock_nanosleep`) are not implemented anywhere, so
  a program that links `-lrt` and calls them fails with undefined symbols.
  *Status (2026-07-02)*: each stub is an 8-byte empty `ar`; `dl`/`pthread`
  resolve from `libc.a`; the realtime symbols above are absent everywhere.
  *Change once*: Nanvix implements the realtime APIs
  ([nanvix/nanvix#2840](https://github.com/nanvix/nanvix/issues/2840)) — the
  empty `librt.a` then resolves real symbols and this note can be dropped.

- **compiler-rt is builtins-only**
  (baremetal build; sanitizers, profile, XRay, ORC, memprof, etc. are all
  `OFF` in `z`).
  *Change once*: Nanvix provides the OS runtime facilities these components need
  ([nanvix/nanvix#2841](https://github.com/nanvix/nanvix/issues/2841)) — then
  flip the corresponding `COMPILER_RT_BUILD_*` flags in `z` selectively
  (profile/coverage first, then UBSan, then ASan).

- **No system dynamic loader (executables are static ELF only)**.
  The driver forces `-Bstatic`, emits no ELF interpreter (`--no-dynamic-linker`
  on `-shared` links), and suppresses `RELRO`/build-id/rosegment. Shared runtime
  libraries can be loaded explicitly with `dlopen`, and both x86 ports have
  startup self-linking of an executable's `DT_NEEDED` dependencies, but no
  `PT_INTERP` system dynamic linker.
  *Change once*: the Nanvix startup dynamic-linking epic lands
  ([nanvix/nanvix#2782](https://github.com/nanvix/nanvix/issues/2782), driver
  item [#2771](https://github.com/nanvix/nanvix/issues/2771)) — then add an
  opt-in dynamic-executable link mode and relax the static-only defaults in the
  driver.
