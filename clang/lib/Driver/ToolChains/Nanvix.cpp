//===--- Nanvix.cpp - Nanvix ToolChain Implementations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Nanvix.h"
#include "Gnu.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Options/Options.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

/// Nanvix Linker
void nanvix::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  const auto &ToolChain = static_cast<const Nanvix &>(getToolChain());
  const Driver &D = ToolChain.getDriver();

  ArgStringList CmdArgs;

  // Silence warning for "clang -g foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_g_Group);
  // and "clang -emit-llvm foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  // and for "clang -w foo.o -o foo".
  Args.ClaimAllArgs(options::OPT_w);

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  if (Args.hasArg(options::OPT_s))
    CmdArgs.push_back("-s");

  const bool IsShared = Args.hasArg(options::OPT_shared);

  if (Args.hasArg(options::OPT_r)) {
    CmdArgs.push_back("-r");
  } else {
    // Nanvix's ELF loader requires all LOAD segments to be page-aligned.
    // LLD's default --build-id adds a .note.gnu.build-id orphan section that
    // can break segment alignment with the Nanvix linker script, and
    // GNU_RELRO creates an extra LOAD segment the loader cannot handle.
    CmdArgs.push_back("--build-id=none");
    CmdArgs.push_back("--no-rosegment");
    CmdArgs.push_back("-z");
    CmdArgs.push_back("norelro");

    if (IsShared) {
      CmdArgs.push_back("-shared");
      // libc/libm objects use the static relocation model; allow the
      // R_386_* text relocations the shared objects emit (matches the
      // in-source libc.so/libm.so link). Nanvix has no ELF interpreter:
      // shared objects are brought in via dlopen, so emit no interpreter.
      CmdArgs.push_back("-z");
      CmdArgs.push_back("notext");
      CmdArgs.push_back("--no-dynamic-linker");
    }
  }

  if (!Args.hasArg(options::OPT_r)) {
    // Nanvix executables are static ELF; force archive selection so -lc/-lm
    // pick libc.a/libm.a even when libc.so/libm.so sit in the same lib
    // directory.
    if (!IsShared)
      CmdArgs.push_back("-Bstatic");

    CmdArgs.push_back("--eh-frame-hdr");
    CmdArgs.push_back("--gc-sections");

    // Nanvix does not provide a standalone libpthread; pthread symbols are
    // supplied by the in-source C library (libc.a) or by the empty stub
    // archives (libdl.a, libpthread.a, librt.a).  Ignore the dependent-library
    // metadata that libc++/libc++abi embed (e.g., "pthread").
    CmdArgs.push_back("--no-dependent-libraries");
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  // CRT startup: Nanvix ships a single in-source startfile (crt0.o, entry
  // _do_start) and no crti/crtbegin/crtend/crtn.  Constructors and destructors
  // run via .preinit_array/.init_array/.fini_array, whose bounds are provided
  // by the Nanvix user linker script (user.ld).  Shared objects carry no crt0.
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r) &&
      !IsShared) {
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crt0.o")));
  }

  // Forward -L (library search paths) and -u (undefined symbol) flags.
  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_u});

  // Library search paths from the toolchain.
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  // User inputs.
  addLinkerCompressDebugSectionsOption(ToolChain, Args, CmdArgs);
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  // Standard libraries.
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                   options::OPT_r)) {
    // Use --start-group/--end-group to resolve circular dependencies
    // between static archives (e.g., libc referencing compiler-rt builtins).
    CmdArgs.push_back("--start-group");

    // C++ standard library.
    if (D.CCCIsCXX() && ToolChain.ShouldLinkCXXStdlib(Args))
      ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);

    // Compiler runtime (compiler-rt builtins).
    AddRunTimeLibs(ToolChain, D, CmdArgs, Args);

    CmdArgs.push_back("-lm");
    CmdArgs.push_back("-lc");

    CmdArgs.push_back("--end-group");
  }

  // Nanvix user linker script (user.ld): it defines the section-boundary and
  // TLS/dynamic symbols that crt0 and the in-source C library reference
  // (__init_array_start, __TLS_START, __TLS_END, __dynsym_end, ...), so a link
  // fails without it.  Supply it automatically -- resolved from the toolchain
  // file-search paths (the sysroot lib/ dirs) -- as a *default* script (LLD's
  // --default-script/-dT) so that an explicit user -T still takes precedence.
  // Skipped for -r/-shared and the -nostdlib/-nostartfiles modes that suppress
  // the default startup environment, and when the user already passed -T.
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r) &&
      !IsShared && !Args.hasArg(options::OPT_T)) {
    // Probe only the toolchain file-search paths (the sysroot lib/ dirs), not
    // the broader GetFilePath() locations (-B prefixes, ResourceDir, the
    // compiler-rt dir), so nothing but the sysroot's own script is picked up;
    // a stray user.ld in the working directory is likewise ignored.  When the
    // script is absent the link falls back to the clearer crt0/libc missing-
    // symbol error instead of gaining a dangling --default-script argument.  A
    // script supplied via -Wl,-T is invisible to the driver, so this omission
    // is intentionally silent.
    for (const auto &Dir : ToolChain.getFilePaths()) {
      SmallString<128> Script(Dir);
      llvm::sys::path::append(Script, "user.ld");
      if (ToolChain.getVFS().exists(Script)) {
        CmdArgs.push_back("--default-script");
        CmdArgs.push_back(Args.MakeArgString(Script));
        break;
      }
    }
  }

  // Linker scripts from -T flags (after the inputs so that scripts can govern
  // the placement of all sections including the crt0 object).
  Args.addAllArgs(CmdArgs, {options::OPT_T});

  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

/// Nanvix ToolChain

Nanvix::Nanvix(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {

  getProgramPaths().push_back(getDriver().Dir);

  if (!D.SysRoot.empty()) {
    // Nanvix ships its in-source C library (crt0.o, libc.a, libm.a, the
    // libdl/libpthread/librt stubs) and the LLVM stage-1 runtimes (libc++,
    // libc++abi, libunwind) under flat lib directories of the sysroot.  Search
    // both $SYSROOT/lib and $SYSROOT/usr/lib so crt0.o and the archives resolve
    // regardless of which prefix the toolchain was staged into.
    for (const char *Dir : {"lib", "usr/lib"}) {
      SmallString<128> P(D.SysRoot);
      llvm::sys::path::append(P, Dir);
      if (getVFS().exists(P))
        getFilePaths().push_back(std::string(P));
    }
  }
}

std::string Nanvix::getCompilerRTPath() const {
  // Compiler-rt builtins are installed in a per-target directory
  // (e.g., lib/clang/21/lib/i686-unknown-nanvix/) but may use the
  // old naming convention (libclang_rt.builtins-i386.a). Point the
  // fallback path at the per-target directory so the arch-suffixed
  // name is found.
  SmallString<128> Path(getDriver().ResourceDir);
  llvm::sys::path::append(Path, "lib", getTriple().str());
  return std::string(Path);
}

void Nanvix::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                       ArgStringList &CC1Args) const {
  const Driver &D = getDriver();

  // Check for -nostdinc, which disables all system include paths.
  if (DriverArgs.hasArg(clang::options::OPT_nostdinc)) {
    return;
  }

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> P(D.ResourceDir);
    llvm::sys::path::append(P, "include");
    addSystemInclude(DriverArgs, CC1Args, P);
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  // Nanvix's in-source C headers live under $SYSROOT/usr/include.
  if (!D.SysRoot.empty()) {
    SmallString<128> P(D.SysRoot);
    llvm::sys::path::append(P, "usr", "include");
    addExternCSystemInclude(DriverArgs, CC1Args, P);
  }

  // Configure-time C include directories. The Nanvix sysroot ships the
  // in-source C headers (e.g. under $SYSROOT/usr/include); a relative
  // C_INCLUDE_DIRS entry is resolved against the sysroot.
  StringRef CIncludeDirs(C_INCLUDE_DIRS);
  if (CIncludeDirs != "") {
    SmallVector<StringRef, 5> dirs;
    CIncludeDirs.split(dirs, ":");
    for (StringRef dir : dirs) {
      StringRef Prefix =
          llvm::sys::path::is_absolute(dir) ? "" : StringRef(D.SysRoot);
      addExternCSystemInclude(DriverArgs, CC1Args, Prefix + dir);
    }
    return;
  }
}

Tool *Nanvix::buildLinker() const {
  return new tools::nanvix::Linker(*this);
}

Tool *Nanvix::buildAssembler() const {
  return new tools::nanvix::Assembler(*this);
}

void Nanvix::AddClangCXXStdlibIncludeArgs(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  // Respect -nostdinc++ (and the include-suppressing -nostdinc/-nostdlibinc).
  // The stage 1 runtimes build (libc++/libc++abi/libunwind) compiles against
  // the in-tree libc++ headers and passes -nostdinc++ specifically so that the
  // toolchain's own '<install>/include/c++/v1' is not searched. Without this
  // guard, those previously-installed headers shadow the C library via
  // '#include_next', breaking the build once the toolchain has been installed.
  if (DriverArgs.hasArg(options::OPT_nostdinc) ||
      DriverArgs.hasArg(options::OPT_nostdincxx) ||
      DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  // Get from '<install>/bin' to '<install>/include/c++/v1'.
  // Note that ToolchainDir can be relative, so we use '..' instead of
  // parent_path.
  llvm::SmallString<128> ToolchainDir(getDriver().Dir); // <install>/bin
  llvm::sys::path::append(ToolchainDir, "..");
  if (getVFS().exists(ToolchainDir)) {
    addSystemInclude(DriverArgs, CC1Args, ToolchainDir + "/include/c++/v1");
    // Target-specific libc++ headers.
    addSystemInclude(DriverArgs, CC1Args,
                     ToolchainDir + "/include/" +
                         getTriple().str() + "/c++/v1/");
    return;
  } else if (DriverArgs.hasArg(options::OPT_v)) {
    llvm::errs() << "ignoring nonexistent directory \"" << ToolchainDir
                 << "\"\n";
  }
}

void Nanvix::AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                                 llvm::opt::ArgStringList &CmdArgs) const {
  switch (GetCXXStdlibType(Args)) {
  case ToolChain::CST_Libcxx:
    CmdArgs.push_back("-lc++");
    CmdArgs.push_back("-lc++abi");
    CmdArgs.push_back("-lunwind");
    break;
  case ToolChain::CST_Libstdcxx:
    getDriver().Diag(diag::err_drv_invalid_stdlib_name)
        << Args.getLastArgValue(options::OPT_stdlib_EQ);
    break;
  }
}
