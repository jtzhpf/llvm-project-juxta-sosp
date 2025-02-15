; RUN: opt -S %loadPolly -basicaa -polly-dependences -analyze -polly-dependences-analysis-type=value-based < %s | FileCheck %s -check-prefix=VALUE
; RUN: opt -S %loadPolly -basicaa -polly-dependences -analyze -polly-dependences-analysis-type=memory-based < %s | FileCheck %s -check-prefix=MEMORY
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"
target triple = "x86_64-pc-linux-gnu"

;     for(i = 0; i < 100; i++ )
; S1:   A[i] = 2;
;
;     for (i = 0; i < 10; i++ )
; S2:   A[i]  = 5;
;
;     for (i = 0; i < 200; i++ )
; S3:   A[i] = 5;

define void @sequential_writes() {
entry:
  %A = alloca [200 x i32]
  br label %S1

S1:
  %indvar.1 = phi i64 [ 0, %entry ], [ %indvar.next.1, %S1 ]
  %arrayidx.1 = getelementptr [200 x i32]* %A, i64 0, i64 %indvar.1
  store i32 2, i32* %arrayidx.1
  %indvar.next.1 = add i64 %indvar.1, 1
  %exitcond.1 = icmp ne i64 %indvar.next.1, 100
  br i1 %exitcond.1, label %S1, label %exit.1

exit.1:
  br label %S2

S2:
  %indvar.2 = phi i64 [ 0, %exit.1 ], [ %indvar.next.2, %S2 ]
  %arrayidx.2 = getelementptr [200 x i32]* %A, i64 0, i64 %indvar.2
  store i32 5, i32* %arrayidx.2
  %indvar.next.2 = add i64 %indvar.2, 1
  %exitcond.2 = icmp ne i64 %indvar.next.2, 10
  br i1 %exitcond.2, label %S2, label %exit.2

exit.2:
  br label %S3

S3:
  %indvar.3 = phi i64 [ 0, %exit.2 ], [ %indvar.next.3, %S3 ]
  %arrayidx.3 = getelementptr [200 x i32]* %A, i64 0, i64 %indvar.3
  store i32 7, i32* %arrayidx.3
  %indvar.next.3 = add i64 %indvar.3, 1
  %exitcond.3 = icmp ne i64 %indvar.next.3, 200
  br i1 %exitcond.3, label %S3 , label %exit.3

exit.3:
  ret void
}

; VALUE: region: 'S1 => exit.3' in function 'sequential_writes':
; VALUE:   RAW dependences:
; VALUE:     {  }
; VALUE:   WAR dependences:
; VALUE:     {  }
; VALUE:   WAW dependences:
; VALUE:     {
; VALUE:       Stmt_S1[i0] -> Stmt_S2[i0] : i0 >= 0 and i0 <= 9;
; VALUE:       Stmt_S2[i0] -> Stmt_S3[i0] : i0 >= 0 and i0 <= 9;
; VALUE:       Stmt_S1[i0] -> Stmt_S3[i0] : i0 >= 10 and i0 <= 99
; VALUE:     }

; MEMORY: region: 'S1 => exit.3' in function 'sequential_writes':
; MEMORY:   RAW dependences:
; MEMORY:     {  }
; MEMORY:   WAR dependences:
; MEMORY:     {  }
; MEMORY:   WAW dependences:
; MEMORY:     {
; MEMORY:       Stmt_S1[i0] -> Stmt_S2[i0] : i0 <= 9 and i0 >= 0;
; MEMORY:       Stmt_S2[i0] -> Stmt_S3[i0] : i0 <= 9 and i0 >= 0;
; MEMORY:       Stmt_S1[i0] -> Stmt_S3[i0] : i0 <= 99 and i0 >= 0
; MEMORY:     }

;     for(i = 0; i < 100; i++ )
; S1:   A[i] = 2;
;
;     for (i = 0; i < 10; i++ )
; S2:   A[i]  = 5;
;
;     for (i = 0; i < 200; i++ )
; S3:   B[i] = A[i];

define void @read_after_writes() {
entry:
  %A = alloca [200 x i32]
  %B = alloca [200 x i32]
  br label %S1

S1:
  %indvar.1 = phi i64 [ 0, %entry ], [ %indvar.next.1, %S1 ]
  %arrayidx.1 = getelementptr [200 x i32]* %A, i64 0, i64 %indvar.1
  store i32 2, i32* %arrayidx.1
  %indvar.next.1 = add i64 %indvar.1, 1
  %exitcond.1 = icmp ne i64 %indvar.next.1, 100
  br i1 %exitcond.1, label %S1, label %exit.1

exit.1:
  br label %S2

S2:
  %indvar.2 = phi i64 [ 0, %exit.1 ], [ %indvar.next.2, %S2 ]
  %arrayidx.2 = getelementptr [200 x i32]* %A, i64 0, i64 %indvar.2
  store i32 5, i32* %arrayidx.2
  %indvar.next.2 = add i64 %indvar.2, 1
  %exitcond.2 = icmp ne i64 %indvar.next.2, 10
  br i1 %exitcond.2, label %S2, label %exit.2

exit.2:
  br label %S3

S3:
  %indvar.3 = phi i64 [ 0, %exit.2 ], [ %indvar.next.3, %S3 ]
  %arrayidx.3.a = getelementptr [200 x i32]* %A, i64 0, i64 %indvar.3
  %arrayidx.3.b = getelementptr [200 x i32]* %B, i64 0, i64 %indvar.3
  %val = load i32* %arrayidx.3.a
  store i32 %val, i32* %arrayidx.3.b
  %indvar.next.3 = add i64 %indvar.3, 1
  %exitcond.3 = icmp ne i64 %indvar.next.3, 200
  br i1 %exitcond.3, label %S3 , label %exit.3

exit.3:
  ret void
}

; VALUE: region: 'S1 => exit.3' in function 'read_after_writes':
; VALUE:   RAW dependences:
; VALUE:     {
; VALUE:       Stmt_S2[i0] -> Stmt_S3[i0] : i0 >= 0 and i0 <= 9;
; VALUE:       Stmt_S1[i0] -> Stmt_S3[i0] : i0 >= 10 and i0 <= 99
; VALUE:     }
; VALUE:   WAR dependences:
; VALUE:     {  }
; VALUE:   WAW dependences:
; VALUE:     {
; VALUE:       Stmt_S1[i0] -> Stmt_S2[i0] : i0 >= 0 and i0 <= 9
; VALUE:     }

; MEMORY: region: 'S1 => exit.3' in function 'read_after_writes':
; MEMORY:   RAW dependences:
; MEMORY:     {
; MEMORY:       Stmt_S2[i0] -> Stmt_S3[i0] : i0 <= 9 and i0 >= 0;
; MEMORY:       Stmt_S1[i0] -> Stmt_S3[i0] : i0 <= 99 and i0 >= 0
; MEMORY:     }
; MEMORY:   WAR dependences:
; MEMORY:     {  }
; MEMORY:   WAW dependences:
; MEMORY:     {
; MEMORY:       Stmt_S1[i0] -> Stmt_S2[i0] : i0 <= 9 and i0 >= 0
; MEMORY:     }

;     for(i = 0; i < 100; i++ )
; S1:   B[i] = A[i];
;
;     for (i = 0; i < 10; i++ )
; S2:   A[i]  = 5;
;
;     for (i = 0; i < 200; i++ )
; S3:   A[i]  = 10;

define void @write_after_read() {
entry:
  %A = alloca [200 x i32]
  %B = alloca [200 x i32]
  br label %S1

S1:
  %indvar.1 = phi i64 [ 0, %entry ], [ %indvar.next.1, %S1 ]
  %arrayidx.1.a = getelementptr [200 x i32]* %A, i64 0, i64 %indvar.1
  %arrayidx.1.b = getelementptr [200 x i32]* %B, i64 0, i64 %indvar.1
  %val = load i32* %arrayidx.1.a
  store i32 %val, i32* %arrayidx.1.b
  %indvar.next.1 = add i64 %indvar.1, 1
  %exitcond.1 = icmp ne i64 %indvar.next.1, 100
  br i1 %exitcond.1, label %S1, label %exit.1

exit.1:
  br label %S2

S2:
  %indvar.2 = phi i64 [ 0, %exit.1 ], [ %indvar.next.2, %S2 ]
  %arrayidx.2 = getelementptr [200 x i32]* %A, i64 0, i64 %indvar.2
  store i32 5, i32* %arrayidx.2
  %indvar.next.2 = add i64 %indvar.2, 1
  %exitcond.2 = icmp ne i64 %indvar.next.2, 10
  br i1 %exitcond.2, label %S2, label %exit.2

exit.2:
  br label %S3

S3:
  %indvar.3 = phi i64 [ 0, %exit.2 ], [ %indvar.next.3, %S3 ]
  %arrayidx.3 = getelementptr [200 x i32]* %A, i64 0, i64 %indvar.3
  store i32 10, i32* %arrayidx.3
  %indvar.next.3 = add i64 %indvar.3, 1
  %exitcond.3 = icmp ne i64 %indvar.next.3, 200
  br i1 %exitcond.3, label %S3 , label %exit.3

exit.3:
  ret void
}

; VALUE: region: 'S1 => exit.3' in function 'write_after_read':
; VALUE:   RAW dependences:
; VALUE:     {
; VALUE:     }
; VALUE:   WAR dependences:
; VALUE:     {
; VALUE:       Stmt_S1[i0] -> Stmt_S2[i0] : i0 <= 9 and i0 >= 0;
; VALUE:       Stmt_S1[i0] -> Stmt_S3[i0] : i0 <= 99 and i0 >= 10
; VALUE:     }
; VALUE:   WAW dependences:
; VALUE:     {
; VALUE:       Stmt_S2[i0] -> Stmt_S3[i0] : i0 >= 0 and i0 <= 9
; VALUE:     }

; MEMORY: region: 'S1 => exit.3' in function 'write_after_read':
; MEMORY:   RAW dependences:
; MEMORY:     {
; MEMORY:     }
; MEMORY:   WAR dependences:
; MEMORY:     {
; MEMORY:        Stmt_S1[i0] -> Stmt_S2[i0] : i0 <= 9 and i0 >= 0;
; MEMORY:        Stmt_S1[i0] -> Stmt_S3[i0] : i0 <= 99 and i0 >= 0
; MEMORY:     }
; MEMORY:   WAW dependences:
; MEMORY:     {
; MEMORY:        Stmt_S2[i0] -> Stmt_S3[i0] : i0 <= 9 and i0 >= 0
; MEMORY:     }

;     for(i = 0; i < 100; i++ )
; S1:   A[i] = 10
;
;     for(i = 0; i < 100; i++ )
; S2:   B[i] = A[i + p];

define void @parametric_offset(i64 %p) {
entry:
  %A = alloca [200 x i32]
  %B = alloca [200 x i32]
  br label %S1

S1:
  %indvar.1 = phi i64 [ 0, %entry ], [ %indvar.next.1, %S1 ]
  %arrayidx.1 = getelementptr [200 x i32]* %A, i64 0, i64 %indvar.1
  store i32 10, i32* %arrayidx.1
  %indvar.next.1 = add i64 %indvar.1, 1
  %exitcond.1 = icmp ne i64 %indvar.next.1, 100
  br i1 %exitcond.1, label %S1, label %exit.1

exit.1:
  br label %S2

S2:
  %indvar.2 = phi i64 [ 0, %exit.1 ], [ %indvar.next.2, %S2 ]
  %sum = add i64 %indvar.2, %p
  %arrayidx.2.a = getelementptr [200 x i32]* %A, i64 0, i64 %sum
  %arrayidx.2.b = getelementptr [200 x i32]* %B, i64 0, i64 %indvar.2
  %val = load i32* %arrayidx.2.a
  store i32 %val, i32* %arrayidx.2.b
  %indvar.next.2 = add i64 %indvar.2, 1
  %exitcond.2 = icmp ne i64 %indvar.next.2, 10
  br i1 %exitcond.2, label %S2, label %exit.2

exit.2:
  ret void
}

; VALUE: region: 'S1 => exit.2' in function 'parametric_offset':
; VALUE:   RAW dependences:
; VALUE:     [p] -> {
; VALUE:       Stmt_S1[i0] -> Stmt_S2[-p + i0] :
; VALUE:           i0 >= p and i0 <= 9 + p and p <= 190 and i0 <= 99 and i0 >= 0
; VALUE:     }
; VALUE:   WAR dependences:
; VALUE:     [p] -> {
; VALUE:     }
; VALUE:   WAW dependences:
; VALUE:     [p] -> {
; VALUE:     }

; MEMORY: region: 'S1 => exit.2' in function 'parametric_offset':
; MEMORY:   RAW dependences:
; MEMORY:     [p] -> {
; MEMORY:       Stmt_S1[i0] -> Stmt_S2[-p + i0] :
; MEMORY:           i0 >= p and i0 <= 99 and i0 >= 0 and i0 <= 9 + p
; MEMORY:     }
; MEMORY:   WAR dependences:
; MEMORY:     [p] -> {
; MEMORY:     }
; MEMORY:   WAW dependences:
; MEMORY:     [p] -> {
; MEMORY:     }
