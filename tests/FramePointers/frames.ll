target triple = "x86_64-unknown-linux-gnu"

define i32 @leaf_none(i32 %x) "frame-pointer"="none" {
  %r = add i32 %x, 1
  ret i32 %r
}

define i32 @leaf_nonleaf(i32 %x) "frame-pointer"="non-leaf" {
  %r = add i32 %x, 2
  ret i32 %r
}

define i32 @leaf_all(i32 %x) "frame-pointer"="all" {
  %r = add i32 %x, 3
  ret i32 %r
}

define i32 @leaf_unspecified(i32 %x) {
  %r = add i32 %x, 4
  ret i32 %r
}

declare void @external() "frame-pointer"="none"
