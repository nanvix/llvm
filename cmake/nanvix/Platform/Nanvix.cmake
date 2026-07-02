# Copyright(c) The Maintainers of Nanvix.
# Licensed under the MIT License.
#
# Minimal CMake platform definition for the Nanvix operating system.
#
# Nanvix is an ELF, Unix-like target. Without this module CMake cannot classify
# CMAKE_SYSTEM_NAME=Nanvix and aborts the stage-1 cross build of the runtimes
# (e.g. llvm/cmake/modules/HandleLLVMOptions.cmake: "Unable to determine
# platform", because neither WIN32, UNIX nor "Generic" is set). Teaching CMake
# that Nanvix is UNIX lets compiler-rt, libunwind, libc++ and libc++abi
# cross-configure against the in-source Nanvix libc.
#
# This file is found by CMake because z's configure_stage1 adds the enclosing
# directory (cmake/nanvix) to CMAKE_MODULE_PATH.

set(NANVIX 1)
set(UNIX 1)

# libdl is shipped as a stub archive inside the Nanvix sysroot; there is no
# separate runtime loader library to link against.
set(CMAKE_DL_LIBS "")

# ELF shared-object conventions, used when the *.so runtime variants are built.
# The Nanvix clang driver supports -shared / -fPIC and emits ELF objects.
set(CMAKE_SHARED_LIBRARY_C_FLAGS "-fPIC")
set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-shared")
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "-fPIC")
set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "-shared")
set(CMAKE_SHARED_LIBRARY_SONAME_C_FLAG "-Wl,-soname,")
set(CMAKE_SHARED_LIBRARY_SONAME_CXX_FLAG "-Wl,-soname,")
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG "-Wl,-rpath,")
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG_SEP ":")

# Standard Unix install/search prefixes. CMAKE_SYSROOT (passed by z) is applied
# on top of these so headers/libraries resolve from the Nanvix sysroot.
include(Platform/UnixPaths)
