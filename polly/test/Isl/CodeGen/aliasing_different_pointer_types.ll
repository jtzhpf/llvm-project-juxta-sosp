; RUN: opt %loadPolly -polly-code-generator=isl -polly-codegen-isl -S < %s | FileCheck %s
;
; Check that we cast the different pointer types correctly before we compare
; them in the RTC's. We use i8* as max pointer type.
;
; CHECK: entry:
; CHECK:   %polly.access.B = getelementptr float** %B, i64 1024
; CHECK:   %polly.access.A = getelementptr double** %A, i64 0
; CHECK:   %[[paBb:[._a-zA-Z0-9]]] = bitcast float** %polly.access.B to i8*
; CHECK:   %[[paAb:[._a-zA-Z0-9]]] = bitcast double** %polly.access.A to i8*
; CHECK:   %[[ALeB:[._a-zA-Z0-9]]] = icmp ule i8* %[[paBb]], %[[paAb]]
; CHECK:   %polly.access.A1 = getelementptr double** %A, i64 1024
; CHECK:   %polly.access.B2 = getelementptr float** %B, i64 0
; CHECK:   %[[paA1b:[._a-zA-Z0-9]]] = bitcast double** %polly.access.A1 to i8*
; CHECK:   %[[paB2b:[._a-zA-Z0-9]]] = bitcast float** %polly.access.B2 to i8*
; CHECK:   %[[A1LeB2:[._a-zA-Z0-9]]] = icmp ule i8* %[[paA1b]], %[[paB2b]]
; CHECK:   %[[le1OrLe2:[._a-zA-Z0-9]]] = or i1 %[[ALeB]], %[[A1LeB2]]
; CHECK:   %[[orAndTrue:[._a-zA-Z0-9]]] = and i1 true, %[[le1OrLe2]]
; CHECK:   br label %polly.split_new_and_old
;
;    void jd(double **A, float **B) {
;      for (int i = 0; i < 1024; i++)
;        A[i] = (double *)B[i];
;    }
;
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"

define void @jd(double** %A, float** %B) {
entry:
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %indvars.iv = phi i64 [ %indvars.iv.next, %for.inc ], [ 0, %entry ]
  %exitcond = icmp ne i64 %indvars.iv, 1024
  br i1 %exitcond, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %arrayidx = getelementptr inbounds float** %B, i64 %indvars.iv
  %tmp = load float** %arrayidx, align 8
  %tmp1 = bitcast float* %tmp to double*
  %arrayidx2 = getelementptr inbounds double** %A, i64 %indvars.iv
  store double* %tmp1, double** %arrayidx2, align 8
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret void
}
