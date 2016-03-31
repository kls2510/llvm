; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops -emit-llvm -o - | FileCheck %s

define i32 @test1() #0 {
; CHECK: @test1
; CHECK-NEXT: entry:
; CHECK-NEXT: br label %structSetup
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %k.06 = phi i32 [ 2, %entry ], [ %add, %for.body ]
  %add = add nsw i32 3, %k.06
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i32 %add
}