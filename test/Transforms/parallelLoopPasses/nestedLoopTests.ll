; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops
; RUN: LD_LIBRARY_PATH=~/lib ./a.out | FileCheck %s

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-freebsd10.1"

@.str = private unnamed_addr constant [12 x i8] c"value : %d\0A\00", align 1

; Function Attrs: nounwind readnone uwtable
define i32 @test1() #0 {
entry:
  %array = alloca [10 x i32], align 16
  %0 = bitcast [10 x i32]* %array to i8*
  call void @llvm.lifetime.start(i64 40, i8* %0) #4
  br label %for.body

for.body:                                         ; preds = %for.inc.13, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.inc.13 ]
  %arrayidx = getelementptr inbounds [10 x i32], [10 x i32]* %array, i64 0, i64 %indvars.iv
  store i32 1, i32* %arrayidx, align 4, !tbaa !1
  br label %for.cond.4.preheader

for.cond.4.preheader:                             ; preds = %for.inc.10, %for.body
  %mul.lcssa31 = phi i32 [ 1, %for.body ], [ %mul, %for.inc.10 ]
  %j.028 = phi i32 [ 0, %for.body ], [ %inc11, %for.inc.10 ]
  %add = add nuw nsw i32 %j.028, 1
  br label %for.body.6

for.body.6:                                       ; preds = %for.body.6, %for.cond.4.preheader
  %1 = phi i32 [ %mul.lcssa31, %for.cond.4.preheader ], [ %mul, %for.body.6 ]
  %k.027 = phi i32 [ 0, %for.cond.4.preheader ], [ %inc, %for.body.6 ]
  %add7 = add nuw nsw i32 %add, %k.027
  %mul = mul nsw i32 %1, %add7
  %inc = add nuw nsw i32 %k.027, 1
  %exitcond = icmp eq i32 %inc, 3
  br i1 %exitcond, label %for.inc.10, label %for.body.6

for.inc.10:                                       ; preds = %for.body.6
  %inc11 = add nuw nsw i32 %j.028, 1
  %exitcond32 = icmp eq i32 %inc11, 3
  br i1 %exitcond32, label %for.inc.13, label %for.cond.4.preheader

for.inc.13:                                       ; preds = %for.inc.10
  store i32 %mul, i32* %arrayidx, align 4, !tbaa !1
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond33 = icmp eq i64 %indvars.iv.next, 10
  br i1 %exitcond33, label %for.end.15, label %for.body

for.end.15:                                       ; preds = %for.inc.13
  %arrayidx16 = getelementptr inbounds [10 x i32], [10 x i32]* %array, i64 0, i64 5
  %2 = load i32, i32* %arrayidx16, align 4, !tbaa !1
  call void @llvm.lifetime.end(i64 40, i8* nonnull %0) #4
  ret i32 %2
}

; Function Attrs: nounwind argmemonly
declare void @llvm.lifetime.start(i64, i8* nocapture) #1

; Function Attrs: nounwind argmemonly
declare void @llvm.lifetime.end(i64, i8* nocapture) #1

; Function Attrs: nounwind readnone uwtable
define i32 @test2() #0 {
entry:
  %array = alloca [200 x i32], align 16
  %0 = bitcast [200 x i32]* %array to i8*
  call void @llvm.lifetime.start(i64 800, i8* %0) #4
  br label %for.cond.1.preheader

for.cond.1.preheader:                             ; preds = %for.inc.13, %entry
  %indvars.iv = phi i32 [ 1225, %entry ], [ %indvars.iv.next, %for.inc.13 ]
  %i.029 = phi i32 [ 0, %entry ], [ %inc14, %for.inc.13 ]
  br label %for.inc.10

for.inc.10:                                       ; preds = %for.cond.1.preheader, %for.inc.10
  %indvars.iv30 = phi i64 [ 0, %for.cond.1.preheader ], [ %indvars.iv.next31, %for.inc.10 ]
  %arrayidx = getelementptr inbounds [200 x i32], [200 x i32]* %array, i64 0, i64 %indvars.iv30
  store i32 %indvars.iv, i32* %arrayidx, align 4, !tbaa !1
  %indvars.iv.next31 = add nuw nsw i64 %indvars.iv30, 1
  %exitcond32 = icmp eq i64 %indvars.iv.next31, 50
  br i1 %exitcond32, label %for.inc.13, label %for.inc.10

for.inc.13:                                       ; preds = %for.inc.10
  %inc14 = add nuw nsw i32 %i.029, 1
  %indvars.iv.next = add nuw nsw i32 %indvars.iv, 50
  %exitcond33 = icmp eq i32 %inc14, 100
  br i1 %exitcond33, label %for.end.15, label %for.cond.1.preheader

for.end.15:                                       ; preds = %for.inc.13
  %arrayidx16 = getelementptr inbounds [200 x i32], [200 x i32]* %array, i64 0, i64 25
  %1 = load i32, i32* %arrayidx16, align 4, !tbaa !1
  call void @llvm.lifetime.end(i64 800, i8* nonnull %0) #4
  ret i32 %1
}

define i32 @test3() #0 {
entry:
  %array = alloca [5 x i32], align 16
  %0 = bitcast [5 x i32]* %array to i8*
  call void @llvm.lifetime.start(i64 20, i8* %0) #4
  br label %for.body

for.body:                                         ; preds = %for.inc.12, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.inc.12 ]
  %index.033 = phi i32 [ 0, %entry ], [ %3, %for.inc.12 ]
  %arrayidx = getelementptr inbounds [5 x i32], [5 x i32]* %array, i64 0, i64 %indvars.iv
  store i32 1, i32* %arrayidx, align 4, !tbaa !1
  br label %for.cond.4.preheader

for.cond.4.preheader:                             ; preds = %for.inc.9, %for.body
  %mul.lcssa35 = phi i32 [ 1, %for.body ], [ %mul, %for.inc.9 ]
  %index.131 = phi i32 [ %index.033, %for.body ], [ %2, %for.inc.9 ]
  %j.030 = phi i32 [ 0, %for.body ], [ %inc10, %for.inc.9 ]
  br label %for.body.6

for.body.6:                                       ; preds = %for.body.6, %for.cond.4.preheader
  %1 = phi i32 [ %mul.lcssa35, %for.cond.4.preheader ], [ %mul, %for.body.6 ]
  %index.229 = phi i32 [ %index.131, %for.cond.4.preheader ], [ %add, %for.body.6 ]
  %k.028 = phi i32 [ 0, %for.cond.4.preheader ], [ %inc, %for.body.6 ]
  %add = add nsw i32 %index.229, 3
  %mul = mul nsw i32 %1, %add
  %inc = add nuw nsw i32 %k.028, 1
  %exitcond = icmp eq i32 %inc, 3
  br i1 %exitcond, label %for.inc.9, label %for.body.6

for.inc.9:                                        ; preds = %for.body.6
  %2 = add i32 %index.131, 9
  %inc10 = add nuw nsw i32 %j.030, 1
  %exitcond36 = icmp eq i32 %inc10, 2
  br i1 %exitcond36, label %for.inc.12, label %for.cond.4.preheader

for.inc.12:                                       ; preds = %for.inc.9
  %3 = add nuw nsw i32 %index.033, 18
  store i32 %mul, i32* %arrayidx, align 4, !tbaa !1
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond37 = icmp eq i64 %indvars.iv.next, 5
  br i1 %exitcond37, label %for.end.14, label %for.body

for.end.14:                                       ; preds = %for.inc.12
  %arrayidx15 = getelementptr inbounds [5 x i32], [5 x i32]* %array, i64 0, i64 2
  %4 = load i32, i32* %arrayidx15, align 8, !tbaa !1
  %add16 = add nsw i32 %4, 90
  call void @llvm.lifetime.end(i64 20, i8* nonnull %0) #4
  ret i32 %add16
}

; Function Attrs: nounwind readnone uwtable
define i32 @test4() #0 {
entry:
  br label %for.cond.1.preheader

for.cond.1.preheader:                             ; preds = %for.inc.22, %entry
  %i.048 = phi i32 [ 0, %entry ], [ %inc23, %for.inc.22 ]
  %acc.047 = phi i32 [ 1, %entry ], [ %mul, %for.inc.22 ]
  br label %for.cond.4.preheader

for.cond.4.preheader:                             ; preds = %for.inc.19, %for.cond.1.preheader
  %acc.146 = phi i32 [ %acc.047, %for.cond.1.preheader ], [ %mul, %for.inc.19 ]
  %j.045 = phi i32 [ 0, %for.cond.1.preheader ], [ %inc20, %for.inc.19 ]
  %add = add nuw nsw i32 %j.045, %i.048
  br label %for.body.6

for.body.6:                                       ; preds = %for.body.6, %for.cond.4.preheader
  %acc.244 = phi i32 [ %acc.146, %for.cond.4.preheader ], [ %mul, %for.body.6 ]
  %k.043 = phi i32 [ 0, %for.cond.4.preheader ], [ %inc, %for.body.6 ]
  %add7 = add nuw nsw i32 %add, %k.043
  %rem = srem i32 %add7, 3
  %add18 = add nsw i32 %rem, 1
  %mul = mul nsw i32 %add18, %acc.244
  %inc = add nuw nsw i32 %k.043, 1
  %exitcond = icmp eq i32 %inc, 3
  br i1 %exitcond, label %for.inc.19, label %for.body.6

for.inc.19:                                       ; preds = %for.body.6
  %inc20 = add nuw nsw i32 %j.045, 1
  %exitcond49 = icmp eq i32 %inc20, 3
  br i1 %exitcond49, label %for.inc.22, label %for.cond.4.preheader

for.inc.22:                                       ; preds = %for.inc.19
  %inc23 = add nuw nsw i32 %i.048, 1
  %exitcond50 = icmp eq i32 %inc23, 5
  br i1 %exitcond50, label %for.end.24, label %for.cond.1.preheader

for.end.24:                                       ; preds = %for.inc.22
  ret i32 %mul
}

; Function Attrs: nounwind readnone uwtable
define i32 @test5() #0 {
entry:
  br label %for.cond.1.preheader

for.cond.1.preheader:                             ; preds = %for.inc.23, %entry
  %acc2.054 = phi i32 [ 0, %entry ], [ %add, %for.inc.23 ]
  %acc.053 = phi i32 [ 1, %entry ], [ %mul, %for.inc.23 ]
  %i.052 = phi i32 [ 0, %entry ], [ %inc24, %for.inc.23 ]
  br label %for.body.3

for.body.3:                                       ; preds = %for.inc.20, %for.cond.1.preheader
  %acc2.151 = phi i32 [ %acc2.054, %for.cond.1.preheader ], [ %add, %for.inc.20 ]
  %acc.150 = phi i32 [ %acc.053, %for.cond.1.preheader ], [ %mul, %for.inc.20 ]
  %j.049 = phi i32 [ 0, %for.cond.1.preheader ], [ %inc21, %for.inc.20 ]
  %add7 = add nuw nsw i32 %j.049, %i.052
  br label %for.body.6

for.body.6:                                       ; preds = %for.body.6, %for.body.3
  %acc.248 = phi i32 [ %acc.150, %for.body.3 ], [ %mul, %for.body.6 ]
  %k.047 = phi i32 [ 0, %for.body.3 ], [ %inc, %for.body.6 ]
  %add8 = add nuw nsw i32 %add7, %k.047
  %rem = srem i32 %add8, 3
  %add19 = add nsw i32 %rem, 1
  %mul = mul nsw i32 %add19, %acc.248
  %inc = add nuw nsw i32 %k.047, 1
  %exitcond = icmp eq i32 %inc, 3
  br i1 %exitcond, label %for.inc.20, label %for.body.6

for.inc.20:                                       ; preds = %for.body.6
  %add = add nsw i32 %acc2.151, %acc.150
  %inc21 = add nuw nsw i32 %j.049, 1
  %exitcond55 = icmp eq i32 %inc21, 3
  br i1 %exitcond55, label %for.inc.23, label %for.body.3

for.inc.23:                                       ; preds = %for.inc.20
  %inc24 = add nuw nsw i32 %i.052, 1
  %exitcond56 = icmp eq i32 %inc24, 5
  br i1 %exitcond56, label %for.end.25, label %for.cond.1.preheader

for.end.25:                                       ; preds = %for.inc.23
  %add26 = add nsw i32 %add, %mul
  ret i32 %add26
}

; Function Attrs: nounwind readnone uwtable
define i32 @test6() #0 {
entry:
  %a = alloca [250000 x i32], align 16
  %0 = bitcast [250000 x i32]* %a to i8*
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

; Function Attrs: nounwind uwtable
define i32 @main() #2 {
entry:
  %call = tail call i32 @test6()
  %call1 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call) #4
  %call2 = tail call i32 @test1()
  %call3 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call2) #4
  %call4 = tail call i32 @test2()
  %call5 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call4) #4
  %call6 = tail call i32 @test3()
  %call7 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call6) #4
  %call8 = tail call i32 @test4()
  %call9 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call8) #4
  %call10 = tail call i32 @test5()
  %call11 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i
  ret i32 0
}

; Function Attrs: nounwind
declare i32 @printf(i8* nocapture readonly, ...) #3

attributes #0 = { nounwind readnone uwtable "disable-tail-calls"="false" "less-p                                                                                                                     ans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "ta
attributes #1 = { nounwind argmemonly }
attributes #2 = { nounwind uwtable "disable-tail-calls"="false" "less-precise-fp                                                                                                                     th"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-feat
attributes #3 = { nounwind "disable-tail-calls"="false" "less-precise-fpmad"="fa                                                                                                                     se" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+
attributes #4 = { nounwind }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.8.0 (https://github.com/kls2510/clang.git f3231de0fc839
!1 = !{!2, !2, i64 0}
!2 = !{!"int", !3, i64 0}
!3 = !{!"omnipotent char", !4, i64 0}
!4 = !{!"Simple C/C++ TBAA"}
attributes #1 = { nounwind argmemonly }
attributes #2 = { nounwind uwtable "disable-tail-calls"="false" "less-precise-fp                                                                                                                     mad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no                                                                                                                     -infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="                                                                                                                     8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-ma                                                                                                                     th"="false" "use-soft-float"="false" }
attributes #3 = { nounwind "disable-tail-calls"="false" "less-precise-fpmad"="fa                                                                                                                     lse" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp                                                                                                                     -math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "targ                                                                                                                     et-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="fal                                                                                                                     se" "use-soft-float"="false" }
attributes #4 = { nounwind }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.8.0 (https://github.com/kls2510/clang.git f3231de0fc839                                                                                                                     bd243c2318188a4a37c704dd8d4) (https://github.com/kls2510/llvm.git a50e944a2d28c5                                                                                                                     dfb42f7c90314703b5fe91f0bc)"}
!1 = !{!2, !2, i64 0}
!2 = !{!"int", !3, i64 0}
!3 = !{!"omnipotent char", !4, i64 0}
!4 = !{!"Simple C/C++ TBAA"}
~
                                                              316,1         Bot
  ret i32 %add18
}

; Function Attrs: nounwind uwtable
define i32 @main() #2 {
entry:
  %call = tail call i32 @test6()
  %call1 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call) #4
  %call2 = tail call i32 @test1()
  %call3 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call2) #4
  %call4 = tail call i32 @test2()
  %call5 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call4) #4
  %call6 = tail call i32 @test3()
  %call7 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call6) #4
  %call8 = tail call i32 @test4()
  %call9 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call8) #4
  %call10 = tail call i32 @test5()
  %call11 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call10) #4
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

!0 = !{!"clang version 3.8.0 (https://github.com/kls2510/clang.git f3231de0fc839bd243c2318188a4a37c704dd8d4) (https://github.com/kls2510/llvm.git a50e944a2d28c5dfb42f7c90314703b5fe91f0bc)"}
!1 = !{!2, !2, i64 0}
!2 = !{!"int", !3, i64 0}
!3 = !{!"omnipotent char", !4, i64 0}
!4 = !{!"Simple C/C++ TBAA"}

; CHECK: value : 124500
; CHECK: value : 8640
; CHECK: value : 6175
; CHECK: value : 1153937818
; CHECK: value : 2033549312
; CHECK: value : 1581265715
