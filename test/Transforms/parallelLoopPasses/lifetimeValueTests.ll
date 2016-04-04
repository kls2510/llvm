; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops -emit-llvm -S -o - | FileCheck %s

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
entry:
  %t0 = alloca i32, align 4
  %vla26 = alloca [10000 x i32], align 16
  %0 = bitcast i32* %t0 to i8*
  ; CHECK: br label %structSetup
  br label %for.cond.1.preheader

for.cond.1.preheader:                             ; preds = %for.cond.cleanup.3, %entry
  %indvars.iv29 = phi i64 [ 0, %entry ], [ %indvars.iv.next30, %for.cond.cleanup.3 ]
  %1 = mul nuw nsw i64 %indvars.iv29, 100
  %arrayidx = getelementptr inbounds [10000 x i32], [10000 x i32]* %vla26, i64 0, i64 %1
  %2 = trunc i64 %indvars.iv29 to i32
  br label %for.body.4

for.cond.cleanup:                                 ; preds = %for.cond.cleanup.3
  %arrayidx11 = getelementptr inbounds [10000 x i32], [10000 x i32]* %vla26, i64 0, i64 505
  %3 = load i32, i32* %arrayidx11, align 4, !tbaa !1
  ret i32 %3

for.cond.cleanup.3:                               ; preds = %for.body.4
  %indvars.iv.next30 = add nuw nsw i64 %indvars.iv29, 1
  %exitcond31 = icmp eq i64 %indvars.iv.next30, 100
  br i1 %exitcond31, label %for.cond.cleanup, label %for.cond.1.preheader

for.body.4:                                       ; preds = %for.body.4, %for.cond.1.preheader
  %indvars.iv = phi i64 [ 0, %for.cond.1.preheader ], [ %indvars.iv.next, %for.body.4 ]
  call void @llvm.lifetime.start(i64 4, i8* %0) #2
  %4 = trunc i64 %indvars.iv to i32
  store i32 %4, i32* %t0, align 4, !tbaa !1
  %5 = trunc i64 %indvars.iv to i32
  %call = call i32 @raydir(i32 %5, i32 %2, i32* nonnull %t0)
  %arrayidx6 = getelementptr inbounds i32, i32* %arrayidx, i64 %indvars.iv
  store i32 %call, i32* %arrayidx6, align 4, !tbaa !1
  call void @llvm.lifetime.end(i64 4, i8* %0) #2
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 100
  br i1 %exitcond, label %for.cond.cleanup.3, label %for.body.4
}
