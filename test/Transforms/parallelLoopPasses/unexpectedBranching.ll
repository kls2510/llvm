; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops -emit-llvm -S -o - | FileCheck %s

; ModuleID = 'branching.c'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-freebsd10.1"

; Function Attrs: nounwind uwtable
define i32 @test1(i32 %i, i32* nocapture %a) #0 {
;CHECK: test1
;CHECK NEXT: entry:
;CHECK NEXT: br label %structSetup[0-9]+
entry:
  br label %for.body

for.body:                                         ; preds = %for.inc, %entry
  %indvars.iv26 = phi i64 [ 0, %entry ], [ %indvars.iv.next27, %for.inc ]
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %indvars.iv26
  %0 = load i32, i32* %arrayidx, align 4, !tbaa !1
  %cmp1 = icmp sgt i32 %0, %i
  br i1 %cmp1, label %if.then, label %for.inc

if.then:                                          ; preds = %for.body
  %dec = add nsw i32 %0, -1
  store i32 %dec, i32* %arrayidx, align 4, !tbaa !1
  br label %for.inc
  
for.inc:                                          ; preds = %for.body, %if.then
  %indvars.iv.next27 = add nuw nsw i64 %indvars.iv26, 1
  %exitcond28 = icmp eq i64 %indvars.iv.next27, 500
  br i1 %exitcond28, label %for.body.6, label %for.body

for.body.6:                                       ; preds = %for.inc, %for.body.6
  %indvars.iv = phi i64 [ %indvars.iv.next, %for.body.6 ], [ 0, %for.inc ]
  %x.024 = phi i32 [ %mul, %for.body.6 ], [ 1, %for.inc ]
  %arrayidx8 = getelementptr inbounds i32, i32* %a, i64 %indvars.iv
  %1 = load i32, i32* %arrayidx8, align 4, !tbaa !1
  %mul = mul nsw i32 %1, %x.024
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 500
  br i1 %exitcond, label %for.end.11, label %for.body.6

for.end.11:                                       ; preds = %for.body.6
  ret i32 %mul
  
  ;CHECK: continue[0-9]+:
  ;CHECK NEXT: call void @release(%struct.dispatch_group_s* %[0-9]+)
  ;CHECK NEXT: br label %structSetup
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
