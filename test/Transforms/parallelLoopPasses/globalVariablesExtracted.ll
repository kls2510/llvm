; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops -emit-llvm -S -o - | FileCheck %s

; ModuleID = 'globalVarTests.c'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-freebsd10.1"

@ACC = global i32 10, align 4
@Arr = common global [1000 x i32] zeroinitializer, align 16
@.str = private unnamed_addr constant [12 x i8] c"value : %d\0A\00", align 1
@.str.1 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

; Function Attrs: nounwind uwtable
define void @test1(i32* nocapture %a) #0 {
; CHECK: test1
; CHECK-NEXT: entry:
; CHECK-NEXT: br label %for.body
entry:
  br label %for.body

for.body:                                         ; preds = %if.end, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %if.end ]
  %rem7 = and i64 %indvars.iv, 1
  %cmp1 = icmp eq i64 %rem7, 0
  br i1 %cmp1, label %if.then, label %if.end

if.then:                                          ; preds = %for.body
  %0 = trunc i64 %indvars.iv to i32
  store i32 %0, i32* @ACC, align 4, !tbaa !1
  br label %if.end

if.end:                                           ; preds = %if.then, %for.body
  %1 = load i32, i32* @ACC, align 4, !tbaa !1
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %indvars.iv
  store i32 %1, i32* %arrayidx, align 4, !tbaa !1
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 1000
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %if.end
  ret void
}

; Function Attrs: nounwind uwtable
define i32 @test2() #0 {
; CHECK: test2
; CHECK-NEXT: entry:
; CHECK-NEXT: %ACC.promoted = load i32, i32* @ACC, align 4, !tbaa !1
; CHECK-NEXT: br label %structSetup
entry:
  %ACC.promoted = load i32, i32* @ACC, align 4, !tbaa !1
  br label %for.body

; CHECK: for.end:                                          
; CHECK-NEXT:  store i32 [[SAVETHIS1:%[0-9]+]], i32* @ACC, align 4, !tbaa !1
; CHECK-NEXT:  ret i32 [[SAVETHIS2:%[0-9]+]]

for.body:                                         ; preds = %for.body, %entry
  %add9 = phi i32 [ %ACC.promoted, %entry ], [ %add, %for.body ]
  %sum.08 = phi i32 [ 0, %entry ], [ %add2.sum.0, %for.body ]
  %i.07 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %add = add nsw i32 %add9, 3
  %cmp1 = icmp sgt i32 %add, 300
  %add2 = select i1 %cmp1, i32 %add, i32 0
  %add2.sum.0 = add nsw i32 %add2, %sum.08
  %inc = add nuw nsw i32 %i.07, 1
  %exitcond = icmp eq i32 %inc, 300
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  store i32 %add, i32* @ACC, align 4, !tbaa !1
  ret i32 %add2.sum.0
  
; CHECK: continue:  
; CHECK: [[SAVETHIS1]] = load i32, i32* {{%[0-9]+}}
; CHECK: [[SAVETHIS2]] = add i32 {{%[0-9]+}}, {{%[0-9]+}}
; CHECK: br label %for.end  

}

; Function Attrs: nounwind uwtable
define void @test3() #0 {
; CHECK: test3
; CHECK-NEXT: entry:
; CHECK-NEXT: %ACC.promoted = load i32, i32* @ACC, align 4, !tbaa !1
; CHECK-NEXT: br label %structSetup
entry:
  %ACC.promoted = load i32, i32* @ACC, align 4, !tbaa !1
  br label %for.body
  
; CHECK:for.end:                                         
; CHECK-NEXT:  store i32 [[SAVETHIS3:%[0-9]+]], i32* @ACC, align 4, !tbaa !1
; CHECK-NEXT:  ret void


for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %add5 = phi i32 [ %ACC.promoted, %entry ], [ %add, %for.body ]
  %arrayidx = getelementptr inbounds [1000 x i32], [1000 x i32]* @Arr, i64 0, i64 %indvars.iv
  %0 = load i32, i32* %arrayidx, align 4, !tbaa !1
  %add = add nsw i32 %add5, %0
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 1000
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  store i32 %add, i32* @ACC, align 4, !tbaa !1
  ret void
  
; CHECK: continue:  
; CHECK: [[SAVETHIS3]] = add i32 {{%[0-9]+}}, {{%[0-9]+}}
; CHECK: br label %for.end  
}

; Function Attrs: nounwind uwtable
define void @test4() #0 {
; CHECK: test4
; CHECK-NEXT: entry:
; CHECK-NEXT: %0 = load i32, i32* @ACC, align 4, !tbaa !1
; CHECK-NEXT: br label %structSetup
entry:
  %0 = load i32, i32* @ACC, align 4, !tbaa !1
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %arrayidx = getelementptr inbounds [1000 x i32], [1000 x i32]* @Arr, i64 0, i64 %indvars.iv
  store i32 %0, i32* %arrayidx, align 4, !tbaa !1
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 1000
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret void
}

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
entry:
  tail call void @test1(i32* getelementptr inbounds ([1000 x i32], [1000 x i32]* @Arr, i64 0, i64 0))
  %call = tail call i32 @test2()
  tail call void @test3()
  %call1 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %call) #2
  %0 = load i32, i32* getelementptr inbounds ([1000 x i32], [1000 x i32]* @Arr, i64 0, i64 998), align 8, !tbaa !1
  %call2 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %0) #2
  %1 = load i32, i32* @ACC, align 4, !tbaa !1
  %call3 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %1) #2
  tail call void @test4()
  %2 = load i32, i32* getelementptr inbounds ([1000 x i32], [1000 x i32]* @Arr, i64 0, i64 998), align 8, !tbaa !1
  %call4 = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i64 0, i64 0), i32 %2) #2
  %putchar = tail call i32 @putchar(i32 10) #2
  ret i32 0
}

; Function Attrs: nounwind
declare i32 @printf(i8* nocapture readonly, ...) #1

; Function Attrs: nounwind
declare i32 @putchar(i32) #2

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.8.0 (https://github.com/kls2510/clang.git f3231de0fc839bd243c2318188a4a37c704dd8d4) (https://github.com/kls2510/llvm.git aa0cc1e767edbb5bee4dc2ff9119bfed9db5499b)"}
!1 = !{!2, !2, i64 0}
!2 = !{!"int", !3, i64 0}
!3 = !{!"omnipotent char", !4, i64 0}
!4 = !{!"Simple C/C++ TBAA"}