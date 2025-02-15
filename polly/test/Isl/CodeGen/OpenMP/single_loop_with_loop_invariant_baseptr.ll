; RUN: opt %loadPolly -tbaa -polly-parallel -polly-parallel-force -polly-parallel-force -polly-ast -analyze < %s | FileCheck %s -check-prefix=AST
; RUN: opt %loadPolly -tbaa -polly-parallel -polly-parallel-force -polly-parallel-force -polly-codegen-isl -S -verify-dom-info < %s | FileCheck %s -check-prefix=IR
; RUN: opt %loadPolly -tbaa -polly-parallel -polly-parallel-force -polly-parallel-force -polly-codegen-isl -S -verify-dom-info < %s | FileCheck %s -check-prefix=IR

; #define N 1024
; float A[N];
;
; void single_parallel_loop(void) {
;   for (long i = 0; i < N; i++)
;     A[i] = 1;
; }

; AST: #pragma simd
; AST: #pragma omp parallel for
; AST: for (int c1 = 0; c1 <= 1023; c1 += 1)
; AST:   Stmt_S(c1);

; IR: @single_parallel_loop.polly.subfn
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"
target triple = "x86_64-unknown-linux-gnu"

define void @single_parallel_loop(float** %A) nounwind {
entry:
  br label %for.i

for.i:
  %indvar = phi i64 [ %indvar.next, %for.inc], [ 0, %entry ]
  %exitcond = icmp ne i64 %indvar, 1024
  br i1 %exitcond, label %S, label %exit

S:
  %ptr = load float** %A,  !tbaa !2
  %scevgep = getelementptr float* %ptr, i64 %indvar
  %val = load float* %scevgep, !tbaa !6
  %sum = fadd float %val, 1.0
  store float %sum, float* %scevgep, !tbaa !6
  br label %for.inc

for.inc:
  %indvar.next = add i64 %indvar, 1
  br label %for.i

exit:
  ret void
}

!2 = metadata !{metadata !"float", metadata !3, i64 0}
!3 = metadata !{metadata !"omnipotent char", metadata !4, i64 0}
!4 = metadata !{metadata !"Simple C/C++ TBAA"}
!6 = metadata !{metadata !"float *ptr", metadata !3, i64 0}
