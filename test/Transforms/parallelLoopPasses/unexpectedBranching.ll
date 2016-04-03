; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops -emit-llvm -S -o - | FileCheck %s

; ModuleID = 'branching.c'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-freebsd10.1"

; Function Attrs: nounwind readnone uwtable
define i32 @test1(i32 %i) #0 {
entry:
  %cmp = icmp sgt i32 %i, 5
  ;CHECK : br i1 %cmp, label %structSetup, label %if.else
  br i1 %cmp, label %for.body, label %if.else

if.else:                                          ; preds = %entry
  %cmp1 = icmp slt i32 %i, 1
  %mul = mul nsw i32 %i, 3
  %mul. = select i1 %cmp1, i32 %mul, i32 3
  ;CHECK : br label %structSetup
  br label %for.body

for.body:                                         ; preds = %if.else, %entry, %for.body
  %j.013 = phi i32 [ %inc, %for.body ], [ 0, %entry ], [ 0, %if.else ]
  %k.112 = phi i32 [ %mul5, %for.body ], [ 5, %entry ], [ %mul., %if.else ]
  %mul5 = mul nsw i32 %k.112, 3
  %inc = add nuw nsw i32 %j.013, 1
  %exitcond = icmp eq i32 %inc, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i32 %mul5
}

; Function Attrs: nounwind readonly uwtable
define i32 @test2(i32 %k, i32* nocapture readonly %a) #1 {
;CHECK : test2
;CHECK : entry:
;CHECK : br label %for.body
entry:
  br label %for.body

for.body:                                         ; preds = %entry, %for.inc
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.inc ]
  %i.06 = phi i32 [ 0, %entry ], [ %inc, %for.inc ]
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %indvars.iv
  %0 = load i32, i32* %arrayidx, align 4, !tbaa !1
  %cmp1 = icmp sgt i32 %0, %k
  %1 = trunc i64 %indvars.iv to i32
  br i1 %cmp1, label %for.end, label %for.inc

for.inc:                                          ; preds = %for.body
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %inc = add nuw nsw i32 %i.06, 1
  %cmp = icmp slt i64 %indvars.iv.next, 500
  br i1 %cmp, label %for.body, label %for.end

for.end:                                          ; preds = %for.body, %for.inc
  %i.0.lcssa = phi i32 [ %1, %for.body ], [ %inc, %for.inc ]
  ret i32 %i.0.lcssa
}

; Function Attrs: nounwind readonly uwtable
define i32 @test3(i32 %k, i32* nocapture readonly %a) #1 {
;CHECK : test2
;CHECK : entry:
;CHECK : br label %for.body
entry:
  br label %for.body

for.cond:                                         ; preds = %for.body
  %cmp = icmp slt i64 %indvars.iv.next, 500
  br i1 %cmp, label %for.body, label %cleanup

for.body:                                         ; preds = %entry, %for.cond
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.cond ]
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %indvars.iv
  %0 = load i32, i32* %arrayidx, align 4, !tbaa !1
  %cmp1 = icmp sgt i32 %0, %k
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  br i1 %cmp1, label %cleanup, label %for.cond

cleanup:                                          ; preds = %for.cond, %for.body
  %retval.0 = phi i32 [ %0, %for.body ], [ 0, %for.cond ]
  ret i32 %retval.0
}

attributes #0 = { nounwind readnone uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readonly uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.8.0 (https://github.com/kls2510/clang.git f3231de0fc839bd243c2318188a4a37c704dd8d4) (https://github.com/kls2510/llvm.git 6eb3b47ae2b61ceeb4592d00b5af3e3deff054c2)"}
!1 = !{!2, !2, i64 0}
!2 = !{!"int", !3, i64 0}
!3 = !{!"omnipotent char", !4, i64 0}
!4 = !{!"Simple C/C++ TBAA"}
