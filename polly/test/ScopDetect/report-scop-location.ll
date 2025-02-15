; RUN: opt %loadPolly -polly-detect -polly-report -disable-output < %s  2>&1 | FileCheck %s
target datalayout = "e-i64:64-f80:128-s:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: nounwind uwtable
define void @foo(float* %A) #0 {
entry:
  br label %entry.split

entry.split:                                      ; preds = %entry
  br label %for.body, !dbg !11

for.body:                                         ; preds = %entry.split, %for.body
  %indvar = phi i64 [ 0, %entry.split ], [ %indvar.next, %for.body ]
  %i.01 = trunc i64 %indvar to i32, !dbg !13
  %arrayidx = getelementptr float* %A, i64 %indvar, !dbg !13
  %conv = sitofp i32 %i.01 to float, !dbg !13
  store float %conv, float* %arrayidx, align 4, !dbg !13
  %indvar.next = add i64 %indvar, 1, !dbg !11
  %exitcond = icmp ne i64 %indvar.next, 100, !dbg !11
  br i1 %exitcond, label %for.body, label %for.end, !dbg !11

for.end:                                          ; preds = %for.body
  ret void, !dbg !14
}

; CHECK: note: Polly detected an optimizable loop region (scop) in function 'foo'
; CHECK: test.c:2: Start of scop
; CHECK: test.c:3: End of scop

; Function Attrs: nounwind uwtable
define void @bar(float* %A) #0 {
entry:
  br label %entry.split

entry.split:                                      ; preds = %entry
  br label %for.body, !dbg !15

for.body:                                         ; preds = %entry.split, %for.body
  %indvar = phi i64 [ 0, %entry.split ], [ %indvar.next, %for.body ]
  %i.01 = trunc i64 %indvar to i32, !dbg !17
  %arrayidx = getelementptr float* %A, i64 %indvar, !dbg !17
  %conv = sitofp i32 %i.01 to float, !dbg !17
  store float %conv, float* %arrayidx, align 4, !dbg !17
  %indvar.next = add i64 %indvar, 1, !dbg !15
  %exitcond = icmp ne i64 %indvar.next, 100, !dbg !15
  br i1 %exitcond, label %for.body, label %for.end, !dbg !15

for.end:                                          ; preds = %for.body
  ret void, !dbg !18
}

; CHECK: note: Polly detected an optimizable loop region (scop) in function 'bar'
; CHECK: test.c:9: Start of scop
; CHECK: test.c:13: End of scop

attributes #0 = { nounwind uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!8, !9}
!llvm.ident = !{!10}

!0 = metadata !{metadata !"0x11\0012\00clang version 3.5 \000\00\000\00\000", metadata !1, metadata !2, metadata !2, metadata !3, metadata !2, metadata !2} ; [ DW_TAG_compile_unit ] [/home/grosser/Projects/polly/git/tools/polly/test.c] [DW_LANG_C99]
!1 = metadata !{metadata !"test.c", metadata !"/home/grosser/Projects/polly/git/tools/polly"}
!2 = metadata !{i32 0}
!3 = metadata !{metadata !4, metadata !7}
!4 = metadata !{metadata !"0x2e\00foo\00foo\00\001\000\001\000\006\00256\000\001", metadata !1, metadata !5, metadata !6, null, void (float*)* @foo, null, null, metadata !2} ; [ DW_TAG_subprogram ] [line 1] [def] [foo]
!5 = metadata !{metadata !"0x29", metadata !1}          ; [ DW_TAG_file_type ] [/home/grosser/Projects/polly/git/tools/polly/test.c]
!6 = metadata !{metadata !"0x15\00\000\000\000\000\000\000", i32 0, null, null, metadata !2, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!7 = metadata !{metadata !"0x2e\00bar\00bar\00\006\000\001\000\006\00256\000\006", metadata !1, metadata !5, metadata !6, null, void (float*)* @bar, null, null, metadata !2} ; [ DW_TAG_subprogram ] [line 6] [def] [bar]
!8 = metadata !{i32 2, metadata !"Dwarf Version", i32 4}
!9 = metadata !{i32 1, metadata !"Debug Info Version", i32 2}
!10 = metadata !{metadata !"clang version 3.5 "}
!11 = metadata !{i32 2, i32 0, metadata !12, null}
!12 = metadata !{metadata !"0xb\002\000\000", metadata !1, metadata !4} ; [ DW_TAG_lexical_block ] [/home/grosser/Projects/polly/git/tools/polly/test.c]
!13 = metadata !{i32 3, i32 0, metadata !12, null}
!14 = metadata !{i32 4, i32 0, metadata !4, null}
!15 = metadata !{i32 9, i32 0, metadata !16, null}
!16 = metadata !{metadata !"0xb\009\000\001", metadata !1, metadata !7} ; [ DW_TAG_lexical_block ] [/home/grosser/Projects/polly/git/tools/polly/test.c]
!17 = metadata !{i32 13, i32 0, metadata !16, null}
!18 = metadata !{i32 14, i32 0, metadata !7, null}

