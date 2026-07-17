// Driver tests for the Nanvix toolchain (i686-unknown-nanvix).
//
// The Nanvix toolchain drives ld.lld directly (instead of going through a gcc
// wrapper) and emits a fixed set of flags required by the Nanvix ELF loader and
// in-source C library. These tests pin that link line.

// The user.ld-injection checks below (CHECK-USERLD / CHECK-USER-T /
// CHECK-NODEFAULT) link against this fake sysroot (%t.sysroot), which ships the
// Nanvix in-source startfile (crt0.o) and the user linker script (user.ld)
// under lib/ so the driver resolves them from the sysroot lib/ search path. The
// earlier link-line checks instead pass a bare --sysroot=%t with no lib/ dir,
// where crt0.o resolves to its plain fallback name and user.ld is absent
// (exercising the graceful-omission path).
// RUN: rm -rf %t.sysroot
// RUN: mkdir -p %t.sysroot/lib
// RUN: touch %t.sysroot/lib/crt0.o %t.sysroot/lib/user.ld

// Default executable link (C): ld.lld is invoked directly with the Nanvix
// loader/runtime flags, the single in-source startfile (crt0.o), and the
// archive-grouped default libraries.
// RUN: %clang -### --target=i686-unknown-nanvix \
// RUN:     --sysroot=%t %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-LD %s
// CHECK-LD: "{{[^"]*}}ld.lld"
// CHECK-LD-SAME: "--sysroot={{[^"]+}}"
// CHECK-LD-SAME: "--build-id=none"
// CHECK-LD-SAME: "--no-rosegment"
// CHECK-LD-SAME: "-z" "norelro"
// CHECK-LD-SAME: "-Bstatic"
// CHECK-LD-SAME: "--eh-frame-hdr"
// CHECK-LD-SAME: "--gc-sections"
// CHECK-LD-SAME: "--no-dependent-libraries"
// CHECK-LD-SAME: "{{[^"]*}}crt0.o"
// CHECK-LD-SAME: "--start-group"
// CHECK-LD-SAME: "-lm" "-lc"
// CHECK-LD-SAME: "--end-group"
// When user.ld is absent from the sysroot the driver omits it gracefully.
// CHECK-LD-NOT: "--default-script"

// C++ link: the C++ runtime (libc++/libc++abi/libunwind) is added inside the
// archive group, ahead of -lm/-lc.
// RUN: %clangxx -### --target=i686-unknown-nanvix \
// RUN:     --sysroot=%t %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-LDXX %s
// CHECK-LDXX: "{{[^"]*}}ld.lld"
// CHECK-LDXX-SAME: "{{[^"]*}}crt0.o"
// CHECK-LDXX-SAME: "--start-group"
// CHECK-LDXX-SAME: "-lc++" "-lc++abi" "-lunwind"
// CHECK-LDXX-SAME: "-lm" "-lc"
// CHECK-LDXX-SAME: "--end-group"

// Shared object: emit a shared library with text relocations allowed and no
// ELF interpreter, and without the crt0 startfile.
// RUN: %clang -### --target=i686-unknown-nanvix -shared \
// RUN:     --sysroot=%t %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-SHARED %s
// CHECK-SHARED: "{{[^"]*}}ld.lld"
// CHECK-SHARED-SAME: "-shared"
// CHECK-SHARED-SAME: "-z" "notext"
// CHECK-SHARED-SAME: "--no-dynamic-linker"
// CHECK-SHARED-NOT: "-Bstatic"
// CHECK-SHARED-NOT: crt0.o

// Relocatable link (-r): forward -r and link nothing else (no crt0, no default
// libraries, no archive group).
// RUN: %clang -### --target=i686-unknown-nanvix -r \
// RUN:     --sysroot=%t %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-RELOC %s
// CHECK-RELOC: "{{[^"]*}}ld.lld"
// CHECK-RELOC-SAME: "-r"
// CHECK-RELOC-NOT: crt0.o
// CHECK-RELOC-NOT: "--start-group"

// Default link auto-adds the Nanvix user linker script as a default script
// (LLD --default-script/-dT), resolved from the sysroot lib/ path, so a bare
// `clang hello.c -o hello` links without an explicit -T. It is added after the
// default library group so the script can govern section placement.
// RUN: %clang -### --target=i686-unknown-nanvix \
// RUN:     --sysroot=%t.sysroot %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-USERLD %s
// CHECK-USERLD: "{{[^"]*}}ld.lld"
// CHECK-USERLD-SAME: "--end-group"
// CHECK-USERLD-SAME: "--default-script" "{{[^"]*}}user.ld"

// An explicit user -T takes precedence: the driver does not inject its default
// script, and forwards the user's script instead.
// RUN: %clang -### --target=i686-unknown-nanvix \
// RUN:     --sysroot=%t.sysroot -T a.ld %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-USER-T %s
// CHECK-USER-T: "{{[^"]*}}ld.lld"
// CHECK-USER-T-NOT: "--default-script"
// CHECK-USER-T: "-T" "a.ld"

// The default user.ld is suppressed for -nostdlib, -nostartfiles, -shared and
// -r even when the script is present in the sysroot.
// RUN: %clang -### --target=i686-unknown-nanvix -nostdlib \
// RUN:     --sysroot=%t.sysroot %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-NODEFAULT %s
// RUN: %clang -### --target=i686-unknown-nanvix -nostartfiles \
// RUN:     --sysroot=%t.sysroot %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-NODEFAULT %s
// RUN: %clang -### --target=i686-unknown-nanvix -shared \
// RUN:     --sysroot=%t.sysroot %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-NODEFAULT %s
// RUN: %clang -### --target=i686-unknown-nanvix -r \
// RUN:     --sysroot=%t.sysroot %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-NODEFAULT %s
// CHECK-NODEFAULT: "{{[^"]*}}ld.lld"
// CHECK-NODEFAULT-NOT: "--default-script"

// A stray user.ld in the current working directory must not be mistaken for the
// toolchain script: user.ld is injected only when it resolves from a toolchain
// search path. Here the sysroot ships no user.ld, but the working directory
// does, and the driver must still omit the default script.
// RUN: rm -rf %t.cwd
// RUN: mkdir -p %t.cwd
// RUN: touch %t.cwd/user.ld
// RUN: cd %t.cwd && %clang -### --target=i686-unknown-nanvix \
// RUN:     --sysroot=%t %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-CWD %s
// CHECK-CWD: "{{[^"]*}}ld.lld"
// CHECK-CWD-NOT: "--default-script"

int main(void) { return 0; }
