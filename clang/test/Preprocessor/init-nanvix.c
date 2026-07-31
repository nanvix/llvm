// RUN: %clang_cc1 -E -dM -ffreestanding -triple=i686-unknown-nanvix < /dev/null | FileCheck -match-full-lines -check-prefix=I686 %s
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=x86_64-unknown-nanvix < /dev/null | FileCheck -match-full-lines -check-prefix=X86_64 %s

// I686-DAG: #define __INTPTR_TYPE__ long int
// I686-DAG: #define __POINTER_WIDTH__ 32
// I686-DAG: #define __SIZE_TYPE__ long unsigned int
// I686-DAG: #define __nanvix__ 1
// I686-DAG: #define __unix__ 1

// X86_64-DAG: #define _LP64 1
// X86_64-DAG: #define __INTPTR_TYPE__ long int
// X86_64-DAG: #define __LP64__ 1
// X86_64-DAG: #define __POINTER_WIDTH__ 64
// X86_64-DAG: #define __SIZE_TYPE__ long unsigned int
// X86_64-DAG: #define __nanvix__ 1
// X86_64-DAG: #define __unix__ 1