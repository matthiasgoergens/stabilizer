target triple = "x86_64-unknown-linux-gnu"

; CHECK: define i32 @weak({{.*}}align 8
define i32 @weak(i32 %n) noinline align 1 {
  ret i32 %n
}

; CHECK: define i32 @strong({{.*}}align 64
define i32 @strong(i32 %n) noinline align 64 {
  ret i32 %n
}

define i32 @main() {
  ret i32 0
}
