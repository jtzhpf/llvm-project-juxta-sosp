; RUN: opt %loadPolly -polly-code-generator=isl -polly-analyze-ir -analyze < %s -stats 2>&1 | FileCheck %s --check-prefix=RTA
; RUN: opt %loadPolly -polly-code-generator=isl -polly-analyze-ir -polly-use-runtime-alias-checks=false -analyze < %s -stats 2>&1 | FileCheck %s --check-prefix=NORTA
; REQUIRES: asserts

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"
target triple = "x86_64-linux-gnu"

define void @f(i32* nocapture %a, i32* nocapture %b) nounwind {
bb.nph:
  %0 = tail call i32 (...)* @rnd() nounwind       ; <i32> [#uses=1]
  %1 = icmp eq i32 %0, 0                          ; <i1> [#uses=1]
  %sel.b = getelementptr inbounds i32* %b, i64 4 
  %iftmp.0.0 = select i1 %1, i32* %sel.b, i32* %a     ; <i32*> [#uses=2]
  br label %bb3

bb3:                                              ; preds = %bb3, %bb.nph
  %i.06 = phi i64 [ 0, %bb.nph ], [ %tmp, %bb3 ]  ; <i64> [#uses=3]
  %scevgep = getelementptr i32* %a, i64 %i.06     ; <i32*> [#uses=1]
  %scevgep7 = getelementptr i32* %iftmp.0.0, i64 %i.06 ; <i32*> [#uses=1]
  %tmp = add i64 %i.06, 1                         ; <i64> [#uses=3]
  %scevgep8 = getelementptr i32* %iftmp.0.0, i64 %tmp ; <i32*> [#uses=1]
  %2 = load i32* %scevgep, align 4                ; <i32> [#uses=1]
  %3 = load i32* %scevgep8, align 4               ; <i32> [#uses=1]
  %4 = shl i32 %3, 1                              ; <i32> [#uses=1]
  %5 = add nsw i32 %4, %2                         ; <i32> [#uses=1]
  store i32 %5, i32* %scevgep7, align 4
  %exitcond = icmp eq i64 %tmp, 64                ; <i1> [#uses=1]
  br i1 %exitcond, label %return, label %bb3

return:                                           ; preds = %bb3
  ret void
}

declare i32 @rnd(...)


; RTA:   1 polly-detect     - Number of regions that a valid part of Scop
; NORTA: 1 polly-detect     - Number of bad regions for Scop: Found base address alias
