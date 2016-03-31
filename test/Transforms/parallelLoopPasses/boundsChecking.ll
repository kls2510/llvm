; RUN: ~/llvm/Debug/bin/clang %s -parallelize-loops -emit-llvm -S -o - | LD_LIBRARY_PATH=~/lib ~/llvm/Debug/bin/lli | FileCheck %s

@.str.1 = private unnamed_addr constant [11 x i8] c"value : %d\00", align 1

declare i32 @printf(i8* nocapture, ...) nounwind

; Function Attrs: nounwind uwtable
define i32 @test1(i32* nocapture readonly %a) #0 {
; CHECK: @test1
; CHECK-NEXT: entry:
; CHECK-NEXT: br label %structSetup
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %k.07 = phi i32 [ 2, %entry ], [ %add, %for.body ]
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %indvars.iv
  %0 = load i32, i32* %arrayidx, align 4, !tbaa !1
  %add = add nsw i32 %0, %k.07
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 500
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  %call = tail call i32 (i8*, ...) @printf(i8* nonnull getelementptr inbounds ([11 x i8], [11 x i8]* @.str.1, i64 0, i64 0), i32 %add) #6
  ret i32 %add
}

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
entry:
  %a = alloca [500 x i32], align 16
  %0 = bitcast [500 x i32]* %a to i8*
  call void @llvm.lifetime.start(i64 2000, i8* %0) #6
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
  %call = call i32 @test1(i32* %arraydecay)
  %2 = load i32, i32* %arraydecay, align 16, !tbaa !1
  call void @llvm.lifetime.end(i64 2000, i8* nonnull %0) #6
  ret i32 %2
}

; CHECK: value: 124750