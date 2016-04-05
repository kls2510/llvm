; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops -emit-llvm -S -o - | FileCheck %s

; ModuleID = 'functions.c'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-freebsd10.1"

@X = global i32 0, align 4

; Function Attrs: nounwind readnone uwtable
define i32 @f1(i32 %k) #0 {
entry:
  %rem2 = and i32 %k, 1
  %retval.0 = add nsw i32 %rem2, %k
  ret i32 %retval.0
}

; Function Attrs: nounwind readnone uwtable
define i32 @test1() #0 {
entry:
  ; CHECK: br label %structSetup
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %sum.07 = phi i32 [ 0, %entry ], [ %add, %for.body ]
  %i.06 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %call = tail call i32 @f1(i32 %i.06)
  %add = add nsw i32 %call, %sum.07
  %inc = add nuw nsw i32 %i.06, 1
  %exitcond = icmp eq i32 %inc, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i32 %add
}

; Function Attrs: nounwind readonly uwtable
define i32 @f2(i32* nocapture readonly %y) #1 {
entry:
  %0 = load i32, i32* %y, align 4, !tbaa !1
  %cmp = icmp sgt i32 %0, 5
  %. = zext i1 %cmp to i32
  ret i32 %.
}

; Function Attrs: nounwind readonly uwtable
define i32 @test2(i32* nocapture readonly %k) #1 {
entry:
  ; CHECK: br label %structSetup
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %sum.07 = phi i32 [ 0, %entry ], [ %add, %for.body ]
  %i.06 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %rem = srem i32 %i.06, 50
  %idxprom = sext i32 %rem to i64
  %arrayidx = getelementptr inbounds i32, i32* %k, i64 %idxprom
  %call = tail call i32 @f2(i32* %arrayidx)
  %add = add nsw i32 %call, %sum.07
  %inc = add nuw nsw i32 %i.06, 1
  %exitcond = icmp eq i32 %inc, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i32 %add
}

; Function Attrs: nounwind uwtable
define i32 @f3(i32* nocapture %y) #2 {
entry:
  %0 = load i32, i32* %y, align 4, !tbaa !1
  %cmp = icmp sgt i32 %0, 5
  br i1 %cmp, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %inc = add nsw i32 %0, 1
  store i32 %inc, i32* %y, align 4, !tbaa !1
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  %1 = load i32, i32* %y, align 4, !tbaa !1
  ret i32 %1
}

; Function Attrs: nounwind uwtable
define i32 @test3(i32* nocapture %k) #2 {
entry:
  ; CHECK-NOT: br label %structSetup
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %sum.06 = phi i32 [ 0, %entry ], [ %add, %for.body ]
  %i.05 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %call = tail call i32 @f3(i32* %k)
  %add = add nsw i32 %call, %sum.06
  %inc = add nuw nsw i32 %i.05, 1
  %exitcond = icmp eq i32 %inc, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i32 %add
}

; Function Attrs: nounwind uwtable
define void @f4(i32 %k) #2 {
entry:
  %cmp = icmp eq i32 %k, 4
  br i1 %cmp, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %0 = load i32, i32* @X, align 4, !tbaa !1
  %inc = add nsw i32 %0, 1
  store i32 %inc, i32* @X, align 4, !tbaa !1
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  ret void
}

; Function Attrs: nounwind uwtable
define void @test4(i32* nocapture readonly %a) #2 {
entry:
  ; CHECK-NOT: br label %structSetup
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %indvars.iv
  %0 = load i32, i32* %arrayidx, align 4, !tbaa !1
  tail call void @f4(i32 %0)
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret void
}

; Function Attrs: nounwind uwtable
define void @f5(i32* nocapture %y) argmemonly {
entry:
  %0 = load i32, i32* %y, align 4, !tbaa !1
  %cmp = icmp sgt i32 %0, 5
  br i1 %cmp, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %inc = add nsw i32 %0, 1
  store i32 %inc, i32* %y, align 4, !tbaa !1
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  ret void
}

; Function Attrs: nounwind argmemonly
declare void @llvm.lifetime.start(i64, i8* nocapture) #1

; Function Attrs: nounwind argmemonly
declare void @llvm.lifetime.end(i64, i8* nocapture) #1

; Function Attrs: nounwind uwtable
define i32 @test5() #3 {
entry:
  %f = alloca i32, align 4
  %0 = bitcast i32* %f to i8*
  ; CHECK: br label %structSetup
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %sum.07 = phi i32 [ 0, %entry ], [ %add, %for.body ]
  %i.06 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  call void @llvm.lifetime.start(i64 4, i8* %0) #4
  store i32 %i.06, i32* %f, align 4, !tbaa !1
  call void @f5(i32* nonnull %f)
  %1 = load i32, i32* %f, align 4, !tbaa !1
  %add = add nsw i32 %1, %sum.07
  call void @llvm.lifetime.end(i64 4, i8* %0) #4
  %inc = add nuw nsw i32 %i.06, 1
  %exitcond = icmp eq i32 %inc, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret i32 %add
}


attributes #0 = { nounwind readnone uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readonly uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.8.0 (https://github.com/kls2510/clang.git f3231de0fc839bd243c2318188a4a37c704dd8d4) (https://github.com/kls2510/llvm.git a50e944a2d28c5dfb42f7c90314703b5fe91f0bc)"}
!1 = !{!2, !2, i64 0}
!2 = !{!"int", !3, i64 0}
!3 = !{!"omnipotent char", !4, i64 0}
!4 = !{!"Simple C/C++ TBAA"}
