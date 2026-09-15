target triple = "x86_64-unknown-linux-gnu"

define i32 @callee(i32 %n) noinline {
  ret i32 %n
}

; CHECK-LABEL: define i64 @address_bits(
; CHECK: [[BITS:%[^ ]+]] = load i64, ptr
; CHECK: and i64 [[BITS]], 7
define i64 @address_bits() {
  %bits = and i64 ptrtoint (ptr @callee to i64), 7
  ret i64 %bits
}

; This is an IR typing test, not a claim that truncated absolute addresses
; can be represented by the final PIE linker's relocation model.
; CHECK-LABEL: define i32 @address_low(
; CHECK: [[LOW:%[^ ]+]] = load i32, ptr
; CHECK: ret i32 [[LOW]]
define i32 @address_low() {
  ret i32 ptrtoint (ptr @callee to i32)
}

; CHECK-LABEL: define ptr @address_pointer(
; CHECK: [[POINTER:%[^ ]+]] = load ptr, ptr
; CHECK: ret ptr [[POINTER]]
define ptr @address_pointer() {
  ret ptr @callee
}

; A PHI operand must be loaded in its incoming block, not at the PHI itself.
; CHECK-LABEL: define i32 @phi_address(
; CHECK: left:
; CHECK: [[INCOMING:%[^ ]+]] = load i32, ptr
; CHECK-NEXT: br label %join
; CHECK: %value = phi i32 [ [[INCOMING]], %left ], [ 0, %right ]
define i32 @phi_address(i1 %choose) {
entry:
  br i1 %choose, label %left, label %right
left:
  br label %join
right:
  br label %join
join:
  %value = phi i32 [ ptrtoint (ptr @callee to i32), %left ], [ 0, %right ]
  ret i32 %value
}

define i32 @main() {
  ret i32 0
}
