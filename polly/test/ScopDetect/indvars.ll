; RUN: opt %loadPolly -analyze -polly-detect -polly-codegen-isl < %s | FileCheck %s
;
; When a region header is part of a loop, then all entering edges of this region
; should not come from the loop but outside the region.

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"
target triple = "x86_64-unknown-linux-gnu"

define void @main(i64* %A) nounwind {
entry:
  br label %for.i

for.i:
  %indvar.i = phi i64 [ 0, %entry ], [ %indvar.next.i, %for.i.backedge ]
  %indvar.next.i = add i64 %indvar.i, 1
  %scevgep = getelementptr i64* %A, i64 %indvar.i
  store i64 %indvar.i, i64* %scevgep, align 4
  br i1 true, label %for.j.preheader, label %for.j2

for.j.preheader:
  br label %for.j

for.j:
  %indvar.j = phi i64 [ %indvar.next.j, %for.j ], [ 0, %for.j.preheader ]
  %indvar.next.j = add i64 %indvar.j, 1
  %exitcond.j = icmp eq i64 %indvar.next.j, 10
  br i1 %exitcond.j, label %for.j2, label %for.j

for.j2:
  fence seq_cst
  br label %for.i.backedge

for.i.backedge:
  %exitcond.i = icmp eq i64 %indvar.next.i, 2048
  br i1 %exitcond.i, label %for.i, label %.end

.end:
  ret void
}

; CHECK: Valid Region for Scop: for.j => for.j2
