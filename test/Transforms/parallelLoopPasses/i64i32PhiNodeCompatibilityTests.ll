; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops -emit-llvm -S -o - | FileCheck %s

; Function Attrs: nounwind readonly uwtable
define i32 @test1() #0 {
; CHECK: @test1
; CHECK-NEXT: entry:
; CHECK-NEXT: br label %structSetup
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %k.06 = phi i32 [ 2, %entry ], [ %mul, %for.body ]
  %mul = mul i32 %k.06, 3
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i32 %mul
}

; Function Attrs: nounwind readonly uwtable
define i32 @test2() #0 {
; CHECK: @test2
; CHECK-NEXT: entry:
; CHECK-NEXT: br label %structSetup
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %k.06 = phi i64 [ 2, %entry ], [ %mul, %for.body ]
  %mul = mul i64 %k.06, 3
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i64 %mul
}

; Function Attrs: nounwind readonly uwtable
define i32 @test3() #0 {
; CHECK: @test3
; CHECK-NEXT: entry:
; CHECK-NEXT: br label %structSetup
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i32 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %k.06 = phi i32 [ 2, %entry ], [ %mul, %for.body ]
  %mul = mul i32 %k.06, 3
  %indvars.iv.next = add nuw nsw i32 %indvars.iv, 1
  %exitcond = icmp eq i32 %indvars.iv.next, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i32 %mul
}

; Function Attrs: nounwind readonly uwtable
define i32 @test4() #0 {
; CHECK: @test4
; CHECK-NEXT: entry:
; CHECK-NEXT: br label %structSetup
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i32 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %k.06 = phi i64 [ 2, %entry ], [ %mul, %for.body ]
  %mul = mul i64 %k.06, 3
  %indvars.iv.next = add nuw nsw i32 %indvars.iv, 1
  %exitcond = icmp eq i32 %indvars.iv.next, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i64 %mul
}

; Function Attrs: nounwind readonly uwtable
define i32 @test6() #0 {
; CHECK: @test6
; CHECK-NEXT: entry:
; CHECK-NEXT: br label %for.body
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i32 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %k.06 = phi i32 [ 2, %entry ], [ %mul, %for.body ]
  %mul = mul i32 %k.06, 3
  %indvars.iv.next = add nuw nsw i32 %indvars.iv, 1
  %indvars.iv.next.exp = sext i32 %indvars.iv.next to i64 
  %exitcond = icmp eq i64  %indvars.iv.next.exp, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i32 %mul
}

; Function Attrs: nounwind readonly uwtable
define i32 @test7() #0 {
; CHECK: @test7
; CHECK-NEXT: entry:
; CHECK-NEXT: br label %structSetup
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %k.06 = phi i64 [ 2, %entry ], [ %mul, %for.body ]
  %mul = mul i64 %k.06, 3
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %indvars.iv.next.exp = trunc i64 %indvars.iv.next to i32 
  %exitcond = icmp eq i32  %indvars.iv.next.exp, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i64 %mul
}

