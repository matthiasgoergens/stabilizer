target triple = "x86_64-unknown-linux-gnu"
@value = thread_local global i32 7
define ptr @address() {
  ret ptr @value
}
define i32 @main() {
  %p = call ptr @address()
  %v = load i32, ptr %p
  %wrong = icmp ne i32 %v, 7
  %result = zext i1 %wrong to i32
  ret i32 %result
}
