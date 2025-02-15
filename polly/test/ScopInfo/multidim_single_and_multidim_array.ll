; RUN: opt %loadPolly -polly-scops -analyze < %s | FileCheck %s
; RUN: opt %loadPolly -polly-scops -polly-allow-nonaffine -analyze < %s | FileCheck %s --check-prefix=NONAFFINE
; RUN: opt %loadPolly -polly-scops -polly-delinearize -analyze < %s | FileCheck %s --check-prefix=DELIN
; RUN: opt %loadPolly -polly-scops -polly-delinearize -polly-allow-nonaffine -analyze < %s | FileCheck %s --check-prefix=DELIN

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"

; void single-and-multi-dimensional-array(long n,float X[n][n]) {
;  for (long i1 = 0; i1 < n; i1++)
;    X[i1][0] = 1;
;
;  for (long i2 = 0; i2 < n; i2++)
;    X[n-1][i2] = 1;
; }
;
; In previous versions of Polly, the second access was detected as single
; dimensional access whereas the first one was detected as multi-dimensional.
; This test case checks that we now consistently delinearize the array accesses.

; CHECK-NOT: Stmt_for_i_1

; NONAFFINE: p0: %n
; NONAFFINE: p1: (4 * (-1 + %n) * %n)
; NONAFFINE: Statements {
; NONAFFINE:   Stmt_for_i_1
; NONAFFINE:         MayWriteAccess :=   [Reduction Type: NONE]
; NONAFFINE:             [n, p_1] -> { Stmt_for_i_1[i0] -> MemRef_X[o0] };
; NONAFFINE:   Stmt_for_i_2
; NONAFFINE:         MustWriteAccess :=  [Reduction Type: NONE]
; NONAFFINE:             [n, p_1] -> { Stmt_for_i_2[i0] -> MemRef_X[o0] : 4o0 = p_1 + 4i0 };

; DELIN: Stmt_for_i_1
; DELIN:   MustWriteAccess :=
; DELIN:      [n] -> { Stmt_for_i_1[i0] -> MemRef_X[i0, 0] };
; DELIN: Stmt_for_i_2
; DELIN:   MustWriteAccess :=
; DELIN:      [n] -> { Stmt_for_i_2[i0] -> MemRef_X[-1 + n, i0] };

define void @single-and-multi-dimensional-array(i64 %n, float* %X) {
entry:
  br label %for.i.1

for.i.1:
  %indvar.1 = phi i64 [ 0, %entry ], [ %indvar.next.1, %for.i.1 ]
  %offset.1 = mul i64 %n, %indvar.1
  %arrayidx.1 = getelementptr float* %X, i64 %offset.1
  store float 1.000000e+00, float* %arrayidx.1
  %indvar.next.1 = add nsw i64 %indvar.1, 1
  %exitcond.1 = icmp ne i64 %indvar.next.1, %n
  br i1 %exitcond.1, label %for.i.1, label %next

next:
  br label %for.i.2

for.i.2:
  %indvar.2 = phi i64 [ 0, %next ], [ %indvar.next.2, %for.i.2 ]
  %offset.2.a = add i64 %n, -1
  %offset.2.b = mul i64 %n, %offset.2.a
  %offset.2.c = add i64 %offset.2.b, %indvar.2
  %arrayidx.2 = getelementptr float* %X, i64 %offset.2.c
  store float 1.000000e+00, float* %arrayidx.2
  %indvar.next.2 = add nsw i64 %indvar.2, 1
  %exitcond.2 = icmp ne i64 %indvar.next.2, %n
  br i1 %exitcond.2, label %for.i.2, label %exit

exit:
  ret void
}
