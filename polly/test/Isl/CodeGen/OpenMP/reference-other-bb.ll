; RUN: opt %loadPolly -polly-parallel -polly-parallel-force -polly-codegen-isl -S -verify-dom-info < %s | FileCheck %s -check-prefix=IR

; IR: @foo.polly.subfn
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @foo(i32 %sendcount, i8* %recvbuf) {
entry:
  br label %sw.bb3

sw.bb3:
  %tmp = bitcast i8* %recvbuf to double*
  %cmp75 = icmp sgt i32 %sendcount, 0
  br i1 %cmp75, label %for.body, label %end

for.body:
  %i.16 = phi i32 [ %inc14, %for.body ], [ 0, %sw.bb3 ]
  %idxprom11 = sext i32 %i.16 to i64
  %arrayidx12 = getelementptr inbounds double* %tmp, i64 %idxprom11
  store double 1.0, double* %arrayidx12, align 8
  %inc14 = add nsw i32 %i.16, 1
  %cmp7 = icmp slt i32 %inc14, %sendcount
  br i1 %cmp7, label %for.body, label %end

end:
  ret void
}
