; RUN: opt %loadPolly -polly-code-generator=isl -polly-ast -analyze < %s | FileCheck %s
;
;    void jd(int *A, int *B, int c) {
;      for (int i = 0; i < 1024; i++)
;        A[i] = B[c - 10] + B[5];
;    }
;
; CHECK: if (1 && (&MemRef_A[1024] <= &MemRef_B[c >= 15 ? 5 : c - 10] || &MemRef_B[c <= 15 ? 6 : c - 9] <= &MemRef_A[0]))
; CHECK:     for (int c1 = 0; c1 <= 1023; c1 += 1)
; CHECK:       Stmt_for_body(c1);
; CHECK: else
; CHECK:    /* original code */
;
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"

define void @jd(i32* %A, i32* %B, i32 %c) {
entry:
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %indvars.iv = phi i64 [ %indvars.iv.next, %for.inc ], [ 0, %entry ]
  %exitcond = icmp ne i64 %indvars.iv, 1024
  br i1 %exitcond, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %sub = add nsw i32 %c, -10
  %idxprom = sext i32 %sub to i64
  %arrayidx = getelementptr inbounds i32* %B, i64 %idxprom
  %tmp = load i32* %arrayidx, align 4
  %arrayidx1 = getelementptr inbounds i32* %B, i64 5
  %tmp1 = load i32* %arrayidx1, align 4
  %add = add nsw i32 %tmp, %tmp1
  %arrayidx3 = getelementptr inbounds i32* %A, i64 %indvars.iv
  store i32 %add, i32* %arrayidx3, align 4
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret void
}
