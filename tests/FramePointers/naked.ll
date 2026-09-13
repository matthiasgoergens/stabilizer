target triple = "x86_64-unknown-linux-gnu"

define void @naked_entry() naked {
  call void asm sideeffect "retq", ""()
  unreachable
}
