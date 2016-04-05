; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops -emit-llvm -S -o - | FileCheck %s

; Function Attrs: nounwind uwtable
define i32 @raydir(i32 %i, i32 %j, i32* nocapture %k) argmemonly {
entry:
  %mul = mul nsw i32 %j, %i
  %0 = load i32, i32* %k, align 4, !tbaa !1
  %add = add nsw i32 %0, %mul
  store i32 %add, i32* %k, align 4, !tbaa !1
  %add1 = add nsw i32 %add, %i
  ret i32 %add1
}

; Function Attrs: nounwind uwtable
define i32 @test1() #0 {
entry:
  %t0 = alloca i32, align 4
  %vla30 = alloca [50000 x i32], align 16
  %0 = bitcast i32* %t0 to i8*
  ; CHECK: br label %structSetup
  br label %for.cond.1.preheader

for.cond.1.preheader:                             ; preds = %for.cond.cleanup.3, %entry
  %indvars.iv33 = phi i64 [ 0, %entry ], [ %indvars.iv.next34, %for.cond.cleanup.3 ]
  %1 = mul nuw nsw i64 %indvars.iv33, 100
  %arrayidx = getelementptr inbounds [50000 x i32], [50000 x i32]* %vla30, i64 0, i64 %1
  %2 = trunc i64 %indvars.iv33 to i32
  br label %for.body.4

for.cond.cleanup:                                 ; preds = %for.cond.cleanup.3
  %arrayidx11 = getelementptr inbounds [50000 x i32], [50000 x i32]* %vla30, i64 0, i64 505
  %3 = load i32, i32* %arrayidx11, align 4, !tbaa !1
  %arrayidx13 = getelementptr inbounds [50000 x i32], [50000 x i32]* %vla30, i64 0, i64 25009
  %4 = load i32, i32* %arrayidx13, align 4, !tbaa !1
  %add = add nsw i32 %4, %3
  %arrayidx15 = getelementptr inbounds [50000 x i32], [50000 x i32]* %vla30, i64 0, i64 40012
  %5 = load i32, i32* %arrayidx15, align 16, !tbaa !1
  %add16 = add nsw i32 %add, %5
  ret i32 %add16

for.cond.cleanup.3:                               ; preds = %for.body.4
  %indvars.iv.next34 = add nuw nsw i64 %indvars.iv33, 1
  %exitcond35 = icmp eq i64 %indvars.iv.next34, 500
  br i1 %exitcond35, label %for.cond.cleanup, label %for.cond.1.preheader

; CHECK: structSetup:
; CHECK-NEXT:  %4 = alloca %ThreadPasser
; CHECK-NEXT:  %5 = alloca %ThreadReturner
; CHECK-NEXT:  %6 = getelementptr inbounds %ThreadPasser, %ThreadPasser* %4, i32 0, i32 0
; CHECK-NEXT:  %7 = alloca i32
; CHECK-NEXT:  store i32* %7, i32** %6
; CHECK-NEXT:  %8 = getelementptr inbounds %ThreadPasser, %ThreadPasser* %4, i32 0, i32 1
; CHECK-NEXT:  store [50000 x i32]* %vla30, [50000 x i32]** %8
; CHECK-NEXT:  %9 = getelementptr inbounds %ThreadPasser, %ThreadPasser* %4, i32 0, i32 2
; CHECK-NEXT:  %10 = getelementptr inbounds %ThreadPasser, %ThreadPasser* %4, i32 0, i32 3

for.body.4:                                       ; preds = %for.body.4, %for.cond.1.preheader
  %indvars.iv = phi i64 [ 0, %for.cond.1.preheader ], [ %indvars.iv.next, %for.body.4 ]
  call void @llvm.lifetime.start(i64 4, i8* %0) #3
  %6 = trunc i64 %indvars.iv to i32
  store i32 %6, i32* %t0, align 4, !tbaa !1
  %7 = trunc i64 %indvars.iv to i32
  %call = call i32 @raydir(i32 %7, i32 %2, i32* nonnull %t0)
  %arrayidx6 = getelementptr inbounds i32, i32* %arrayidx, i64 %indvars.iv
  store i32 %call, i32* %arrayidx6, align 4, !tbaa !1
  call void @llvm.lifetime.end(i64 4, i8* %0) #3
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 100
  br i1 %exitcond, label %for.cond.cleanup.3, label %for.body.4
}

; Function Attrs: nounwind uwtable
define i32 @test2() #0 {
entry:
  %t0 = alloca i32, align 4
  %vla30 = alloca [50000 x i32], align 16
  ; CHECK: br label %structSetup
  br label %for.cond.1.preheader

for.cond.1.preheader:                             ; preds = %for.cond.cleanup.3, %entry
  %indvars.iv33 = phi i64 [ 0, %entry ], [ %indvars.iv.next34, %for.cond.cleanup.3 ]
  %0 = mul nuw nsw i64 %indvars.iv33, 100
  %arrayidx = getelementptr inbounds [50000 x i32], [50000 x i32]* %vla30, i64 0, i64 %0
  %1 = trunc i64 %indvars.iv33 to i32
  br label %for.body.4

for.cond.cleanup:                                 ; preds = %for.cond.cleanup.3
  %arrayidx11 = getelementptr inbounds [50000 x i32], [50000 x i32]* %vla30, i64 0, i64 505
  %2 = load i32, i32* %arrayidx11, align 4, !tbaa !1
  %arrayidx13 = getelementptr inbounds [50000 x i32], [50000 x i32]* %vla30, i64 0, i64 25009
  %3 = load i32, i32* %arrayidx13, align 4, !tbaa !1
  %add = add nsw i32 %3, %2
  %arrayidx15 = getelementptr inbounds [50000 x i32], [50000 x i32]* %vla30, i64 0, i64 40012
  %4 = load i32, i32* %arrayidx15, align 16, !tbaa !1
  %add16 = add nsw i32 %add, %4
  ret i32 %add16

for.cond.cleanup.3:                               ; preds = %for.body.4
  %indvars.iv.next34 = add nuw nsw i64 %indvars.iv33, 1
  %exitcond35 = icmp eq i64 %indvars.iv.next34, 500
  br i1 %exitcond35, label %for.cond.cleanup, label %for.cond.1.preheader

; CHECK:  structSetup:                                  
; CHECK-NEXT:  %3 = alloca %ThreadPasser.2
; CHECK-NEXT:  %4 = alloca %ThreadReturner.3
; CHECK-NEXT:  %5 = getelementptr inbounds %ThreadPasser.2, %ThreadPasser.2* %3, i32 0, i32 0
; CHECK-NEXT:  %6 = alloca i32
; CHECK-NEXT:  store i32* %6, i32** %5
; CHECK-NEXT:  %7 = getelementptr inbounds %ThreadPasser.2, %ThreadPasser.2* %3, i32 0, i32 1
; CHECK-NEXT:  store [50000 x i32]* %vla30, [50000 x i32]** %7
; CHECK-NEXT:  %8 = getelementptr inbounds %ThreadPasser.2, %ThreadPasser.2* %3, i32 0, i32 2
; CHECK-NEXT: %9 = getelementptr inbounds %ThreadPasser.2, %ThreadPasser.2* %3, i32 0, i32 3
  
for.body.4:                                       ; preds = %for.body.4, %for.cond.1.preheader
  %indvars.iv = phi i64 [ 0, %for.cond.1.preheader ], [ %indvars.iv.next, %for.body.4 ]
  %cast = bitcast i32* %t0 to i8*
  call void @llvm.lifetime.start(i64 4, i8* %cast) #3
  %5 = trunc i64 %indvars.iv to i32
  store i32 %5, i32* %t0, align 4, !tbaa !1
  %6 = trunc i64 %indvars.iv to i32
  %call = call i32 @raydir(i32 %6, i32 %1, i32* nonnull %t0)
  %arrayidx6 = getelementptr inbounds i32, i32* %arrayidx, i64 %indvars.iv
  store i32 %call, i32* %arrayidx6, align 4, !tbaa !1
  call void @llvm.lifetime.end(i64 4, i8* %cast) #3
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 100
  br i1 %exitcond, label %for.cond.cleanup.3, label %for.body.4
}

; CHECK: @threadFunction(i8*)
; CHECK: %loadVal_2 = load i32*, i32** %loadVal_1
; CHECK-NEXT: %1 = bitcast i32* %loadVal_2 to i8*
; CHECK: call void @llvm.lifetime.start(i64 4, i8* %1) #5
; CHECK: call void @llvm.lifetime.end(i64 4, i8* %1) #5

; CHECK: @threadFunction.1(i8*)
; CHECK: %loadVal_2 = load i32*, i32** %loadVal_1
; CHECK-NEXT: %1 = bitcast i32* %loadVal_2 to i8*
; CHECK: %6 = phi i64 [ 0, %for.cond.1.preheader_ ], [ %10, %for.body.4_ ]
; CHECK-NEXT: call void @llvm.lifetime.start(i64 4, i8* %1) #5
; CHECK: call void @llvm.lifetime.end(i64 4, i8* %1) #5
