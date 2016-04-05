; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops -emit-llvm -S -o - | FileCheck %s

; ModuleID = 'lifetimeTest2.c'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-freebsd10.1"

@.str = private unnamed_addr constant [12 x i8] c"value : %d\0A\00", align 1

; Function Attrs: nounwind readnone uwtable
define i32 @test1() #0 {
entry:
  %a = alloca [250000 x i32], align 16
  %0 = bitcast [250000 x i32]* %a to i8*
  ; CHECK: br label %structSetup
  br label %for.body

for.body:                                         ; preds = %for.end.12, %entry
  %i.040 = phi i32 [ 0, %entry ], [ %inc20, %for.end.12 ]
  %sum.039 = phi i32 [ 0, %entry ], [ %add18, %for.end.12 ]
  call void @llvm.lifetime.start(i64 1000000, i8* %0) #4
  br label %for.body.3

for.body.3:                                       ; preds = %for.end, %for.body
  %indvars.iv42 = phi i64 [ 0, %for.body ], [ %indvars.iv.next43, %for.end ]
  %1 = mul nuw nsw i64 %indvars.iv42, 500
  %arrayidx = getelementptr inbounds [250000 x i32], [250000 x i32]* %a, i64 0, i64 %1
  %2 = shl i64 %indvars.iv42, 2
  br label %for.body.6

for.body.6:                                       ; preds = %for.body.6, %for.body.3
  %indvars.iv = phi i64 [ 0, %for.body.3 ], [ %indvars.iv.next, %for.body.6 ]
  %3 = add nuw nsw i64 %indvars.iv, %2
  %arrayidx9 = getelementptr inbounds i32, i32* %arrayidx, i64 %indvars.iv
  %4 = trunc i64 %3 to i32
  store i32 %4, i32* %arrayidx9, align 4, !tbaa !1
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 500
  br i1 %exitcond, label %for.end, label %for.body.6

for.end:                                          ; preds = %for.body.6
  %indvars.iv.next43 = add nuw nsw i64 %indvars.iv42, 1
  %exitcond46 = icmp eq i64 %indvars.iv.next43, 500
  br i1 %exitcond46, label %for.end.12, label %for.body.3

for.end.12:                                       ; preds = %for.end
  %add15 = mul nuw nsw i32 %i.040, 26
  %rem = srem i32 %add15, 500
  %idxprom16 = sext i32 %rem to i64
  %arrayidx17 = getelementptr inbounds [250000 x i32], [250000 x i32]* %a, i64 0, i64 %idxprom16
  %5 = load i32, i32* %arrayidx17, align 4, !tbaa !1
  %add18 = add nsw i32 %5, %sum.039
  call void @llvm.lifetime.end(i64 1000000, i8* nonnull %0) #4
  %inc20 = add nuw nsw i32 %i.040, 1
  %exitcond47 = icmp eq i32 %inc20, 500
  br i1 %exitcond47, label %for.end.21, label %for.body

for.end.21:                                       ; preds = %for.end.12
  ret i32 %add18
}

; Function Attrs: nounwind argmemonly
declare void @llvm.lifetime.start(i64, i8* nocapture) #1

; Function Attrs: nounwind argmemonly
declare void @llvm.lifetime.end(i64, i8* nocapture) #1

; Function Attrs: nounwind uwtable
define i32 @main() #2 {
entry:
  %call = tail call i32 @test1()
  %call1 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call) #4
  ret i32 0
}

; Function Attrs: nounwind
declare i32 @printf(i8* nocapture readonly, ...) #3

attributes #0 = { nounwind readnone uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind argmemonly }
attributes #2 = { nounwind uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { nounwind "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #4 = { nounwind }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.8.0 (https://github.com/kls2510/clang.git f3231de0fc839bd243c2318188a4a37c704dd8d4) (https://github.com/kls2510/llvm.git 325d61eceb3a76c595d38bf2ba8b10c66255c0f0)"}
!1 = !{!2, !2, i64 0}
!2 = !{!"int", !3, i64 0}
!3 = !{!"omnipotent char", !4, i64 0}
!4 = !{!"Simple C/C++ TBAA"}