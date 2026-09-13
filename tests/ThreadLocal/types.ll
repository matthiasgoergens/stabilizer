; RUN: opt --load-pass-plugin=../../LLVMStabilizer.so -passes=stabilize -stabilize-code=true -S %s
@local = thread_local(localexec) global i64 7
@ordinary = global i64 9
@array = thread_local(localexec) global [4 x i32] zeroinitializer

declare ptr @llvm.threadlocal.address.p0(ptr)

; CHECK-LABEL: define ptr @address()
; CHECK: call ptr @llvm.threadlocal.address.p0(ptr @local)
; CHECK-NEXT: ret ptr
define ptr @address() {
  %p = call ptr @llvm.threadlocal.address.p0(ptr @local)
  ret ptr %p
}

; Direct TLS operands are legal IR too: do not cache an address in a table.
; CHECK-LABEL: define i64 @direct()
; CHECK: load i64, ptr @local
define i64 @direct() {
  %v = load i64, ptr @local
  ret i64 %v
}

; CHECK-LABEL: define ptr @element()
; CHECK: ret ptr getelementptr{{.*}}@array
define ptr @element() {
  ret ptr getelementptr ([4 x i32], ptr @array, i64 0, i64 2)
}

; Non-TLS references must still use the ordinary relocation-table path.
; CHECK-LABEL: define ptr @control()
; CHECK: load ptr, ptr @control.relocation_table
define ptr @control() {
  ret ptr @ordinary
}
