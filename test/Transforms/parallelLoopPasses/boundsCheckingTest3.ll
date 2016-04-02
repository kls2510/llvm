; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops
; RUN: LD_LIBRARY_PATH=~/lib ./a.out | FileCheck %s

; ModuleID = 'REGRESSION.ll'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-freebsd10.1"

@.str = private unnamed_addr constant [11 x i8] c"value : %d\00", align 1

; Function Attrs: nounwind uwtable
define i32 @test3(i32* nocapture readonly %a) #0 {
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %k.08 = phi i32 [ 2, %entry ], [ %add, %for.body ]
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %indvars.iv
  %0 = load i32, i32* %arrayidx, align 4, !tbaa !1
  %add = add nsw i32 %0, %k.08
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 12
  %cmp = icmp slt i64 %indvars.iv.next, 500
  br i1 %cmp, label %for.body, label %for.end

for.end:                                          ; preds = %for.body
  %call = tail call i32 (i8*, ...) @printf(i8* nonnull getelementptr inbounds ([11 x i8], [11 x i8]* @.str, i64 0, i64 0), i32 %add) #3
  ret i32 %add
}

; Function Attrs: nounwind argmemonly
declare void @llvm.lifetime.start(i64, i8* nocapture) #1

; Function Attrs: nounwind
declare i32 @printf(i8* nocapture readonly, ...) #2

; Function Attrs: nounwind argmemonly
declare void @llvm.lifetime.end(i64, i8* nocapture) #1

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
entry:
  %a = alloca [500 x i32], align 16
  %0 = bitcast [500 x i32]* %a to i8*
  call void @llvm.lifetime.start(i64 2000, i8* %0) #3
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %arrayidx = getelementptr inbounds [500 x i32], [500 x i32]* %a, i64 0, i64 %indvars.iv
  %1 = trunc i64 %indvars.iv to i32
  store i32 %1, i32* %arrayidx, align 4, !tbaa !1
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  %arraydecay = getelementptr inbounds [500 x i32], [500 x i32]* %a, i64 0, i64 0
  %call = call i32 @test3(i32* %arraydecay)
  %2 = load i32, i32* %arraydecay, align 16, !tbaa !1
  call void @llvm.lifetime.end(i64 2000, i8* nonnull %0) #3
  ret i32 %2
}

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind argmemonly }
attributes #2 = { nounwind "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { nounwind }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.8.0 (https://github.com/kls2510/clang.git f3231de0fc839bd243c2318188a4a37c704dd8d4) (https://github.com/kls2510/llvm.git 8802aa6075c462267d29f715e61c07ae03e3c748)"}
!1 = !{!2, !2, i64 0}
!2 = !{!"int", !3, i64 0}
!3 = !{!"omnipotent char", !4, i64 0}
!4 = !{!"Simple C/C++ TBAA"}

; CHECK: value : 10334
