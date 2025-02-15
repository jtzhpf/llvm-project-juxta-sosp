; RUN: opt %loadPolly -basicaa -polly-parallel -polly-parallel-force -polly-ast -analyze < %s | FileCheck %s -check-prefix=AST
; RUN: opt %loadPolly -basicaa -polly-parallel -polly-parallel-force -polly-codegen-isl -S -verify-dom-info < %s | FileCheck %s -check-prefix=IR
; RUN: opt %loadPolly -basicaa -polly-parallel -polly-parallel-force -polly-codegen-isl -S -verify-dom-info < %s | FileCheck %s -check-prefix=IR

; The interesting part of this test case is the instruction:
;   %tmp = bitcast i8* %call to i64**
; which is not part of the scop. In the SCEV based code generation not '%tmp',
; but %call is a parameter of the SCoP and we need to make sure its value is
; properly forwarded to the subfunction.

; AST: #pragma omp parallel for
; AST: for (int c1 = 0; c1 < cols; c1 += 1)
; AST:   Stmt_for_body(c1);

; IR: @foo.polly.subfn

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @foo(i64 %cols, i8* noalias %call) {
entry:
  %tmp = bitcast i8* %call to i64**
  br label %for.body

for.body:
  %indvar = phi i64 [ %indvar.next, %for.body ], [ 0, %entry ]
  %arrayidx = getelementptr inbounds i64** %tmp, i64 0
  %tmp1 = load i64** %arrayidx, align 8
  %arrayidx.2 = getelementptr inbounds i64* %tmp1, i64 %indvar
  store i64 1, i64* %arrayidx.2, align 4
  %indvar.next = add nsw i64 %indvar, 1
  %cmp = icmp slt i64 %indvar.next, %cols
  br i1 %cmp, label %for.body, label %end

end:
  ret void
}

; Another variation of this test case, now with even more of the index
; expression defined outside of the scop.

; AST: #pragma omp parallel for
; AST: for (int c1 = 0; c1 < cols; c1 += 1)
; AST:   Stmt_for_body(c1);

; IR: @bar.polly.subfn

define void @bar(i64 %cols, i8* noalias %call) {
entry:
  %tmp = bitcast i8* %call to i64**
  %arrayidx = getelementptr inbounds i64** %tmp, i64 0
  br label %for.body

for.body:
  %indvar = phi i64 [ %indvar.next, %for.body ], [ 0, %entry ]
  %tmp1 = load i64** %arrayidx, align 8
  %arrayidx.2 = getelementptr inbounds i64* %tmp1, i64 %indvar
  store i64 1, i64* %arrayidx.2, align 4
  %indvar.next = add nsw i64 %indvar, 1
  %cmp = icmp slt i64 %indvar.next, %cols
  br i1 %cmp, label %for.body, label %end

end:
  ret void
}
