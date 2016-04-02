; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops
; RUN: LD_LIBRARY_PATH=~/lib ./a.out | FileCheck %s

