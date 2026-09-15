import Std

namespace LoopStudy

@[inline] def step (i : USize) (x : UInt64) : UInt64 :=
  let x := (x ^^^ (i.toUInt64 + 0x9e3779b97f4a7c15)) * 0xbf58476d1ce4e5b9
  let x := x ^^^ (x >>> 29)
  let x := x * 0x94d049bb133111eb
  x ^^^ (x >>> 31)

@[noinline] def idWhile (n : USize) (seed : UInt64) : UInt64 := Id.run do
  let mut i : USize := 0
  let mut x := seed
  while i < n do
    x := step i x
    i := i + 1
  return x

@[noinline] def ioWhile (n : USize) (seed : UInt64) : IO UInt64 := do
  let mut i : USize := 0
  let mut x := seed
  while i < n do
    x := step i x
    i := i + 1
  return x

@[noinline] def forNat (n : USize) (seed : UInt64) : UInt64 := Id.run do
  let mut x := seed
  for i in [0:n.toNat] do
    x := step i.toUSize x
  return x

@[noinline] def forUSize (n : USize) (seed : UInt64) : UInt64 := Id.run do
  let mut x := seed
  for i in (0 : USize)...n do
    x := step i x
  return x

@[noinline] partial def tailScalar (n i : USize) (x : UInt64) : UInt64 :=
  if i < n then tailScalar n (i + 1) (step i x) else x

@[noinline] partial def tailPair (n : USize) (s : USize × UInt64) : UInt64 :=
  if s.1 < n then tailPair n (s.1 + 1, step s.1 s.2) else s.2

@[noinline] partial def tailPairReturn (n : USize) (s : USize × UInt64) : USize × UInt64 :=
  if s.1 < n then tailPairReturn n (s.1 + 1, step s.1 s.2) else s

structure State where
  i : USize
  x : UInt64

@[noinline] def whileStruct (n : USize) (seed : UInt64) : UInt64 := Id.run do
  let mut s : State := ⟨0, seed⟩
  while s.i < n do
    s := ⟨s.i + 1, step s.i s.x⟩
  return s.x

@[noinline] partial def tailStruct (n : USize) (s : State) : UInt64 :=
  if s.i < n then tailStruct n ⟨s.i + 1, step s.i s.x⟩ else s.x

@[noinline] def tailFuel : Nat → USize → UInt64 → UInt64
  | 0, _, x => x
  | k + 1, i, x => tailFuel k (i + 1) (step i x)

def runVariant (name : String) (n : USize) (seed : UInt64) : IO UInt64 := do
  match name with
  | "id-while" => return idWhile n seed
  | "io-while" => ioWhile n seed
  | "for-nat" => return forNat n seed
  | "for-usize" => return forUSize n seed
  | "tail-scalar" => return tailScalar n 0 seed
  | "tail-pair" => return tailPair n (0, seed)
  | "tail-pair-return" => return (tailPairReturn n (0, seed)).2
  | "while-struct" => return whileStruct n seed
  | "tail-struct" => return tailStruct n ⟨0, seed⟩
  | "tail-fuel" => return tailFuel n.toNat 0 seed
  | _ => throw (IO.userError s!"unknown variant: {name}")

end LoopStudy

def main (args : List String) : IO Unit := do
  let variant := args.getD 0 "id-while"
  let n := (args.getD 1 "1000").toNat!.toUSize
  let seed := (args.getD 2 "2611923443488327891").toNat!.toUInt64
  let warm ← LoopStudy.runVariant variant (n / 2) seed
  let start ← IO.monoNanosNow
  let result ← LoopStudy.runVariant variant n seed
  let stop ← IO.monoNanosNow
  IO.println s!"{stop - start} {result} {warm}"
