module


public section

set_option compiler.check true
set_option compiler.postponeCompile false

namespace AdlerTiming

def modAdler : Nat := 65521
abbrev State := Nat × Nat

/- The helper and update body intentionally retain their default out-of-line
   status.  This is the path under test for the private Nat split pass. -/
def updateByte (s : State) (byte : UInt8) : State :=
  let a := (s.1 + byte.toNat) % modAdler
  let b := (s.2 + a) % modAdler
  (a, b)

def helper (s : State) (data : ByteArray) : State :=
  data.data.foldl updateByte s

def directHelper (s : State) (data : ByteArray) : State :=
  data.foldl updateByte s

/- An independently written indexed recurrence serves as the scalar negative
   control.  Its update arithmetic is the same as the helper's body. -/
partial def referenceLoop (data : ByteArray) (i : Nat) (a b : Nat) : State :=
  if h : i < data.size then
    let byte := data[i]
    let nextA := (a + byte.toNat) % 65521
    let nextB := (b + nextA) % 65521
    referenceLoop data (i + 1) nextA nextB
  else (a, b)

def reference (s : State) (data : ByteArray) : State :=
  referenceLoop data 0 s.1 s.2

/- This is the high-byte 32-bit LCG used by AdlerWithNat.  The seed argument
   is retained in the generator shape so the generated input has the same
   recurrence and byte stream as that correctness fixture. -/
partial def makeData (n i seed state : Nat) (out : ByteArray) : ByteArray :=
  if i < n then
    let byte := (state / (2^24)) % 256
    makeData n (i + 1) seed ((state * 1664525 + 1013904223) % (2^32))
      (out.push byte.toUInt8)
  else out

def parseNat (name value : String) : IO Nat :=
  match value.toNat? with
  | some x => pure x
  | none => throw <| IO.userError s!"invalid {name}: {value}"

def maxSize : Nat := 1024 * 1024
def maxRepetitions : Nat := 8
def maxTimedBytes : Nat := 2 * 1024 * 1024

def parseBoundedNat (name value : String) (limit : Nat) : IO Nat := do
  let x ← parseNat name value
  if x ≤ limit then pure x
  else throw <| IO.userError s!"{name} exceeds private bound {limit}: {x}"

def checkTimedBytes (size repetitions : Nat) : IO Unit :=
  if repetitions = 0 || size ≤ maxTimedBytes / repetitions then pure ()
  else throw <| IO.userError "size * repetitions exceeds private timed-byte bound"

@[noinline] def runKernel (kernel : String) (s : State) (data : ByteArray) : IO State :=
  match kernel with
  | "helper" => pure (helper s data)
  | "direct" => pure (directHelper s data)
  | "reference" => pure (reference s data)
  | _ => throw <| IO.userError s!"invalid kernel: {kernel}"

/- IO sequencing and the returned state dependency keep every repetition in
   the timed region observable. -/
@[noinline] partial def repeatKernel (kernel : String) (data : ByteArray)
    (repetitions : Nat) (s : State) : IO State :=
  if repetitions = 0 then pure s
  else do
    let next ← runKernel kernel s data
    repeatKernel kernel data (repetitions - 1) next

@[noinline] def bench (kernel : String) (size repetitions seed : Nat) : IO Unit := do
  let data := makeData size 0 seed seed ByteArray.empty
  let initial : State := (1, 0)
  let warm ← runKernel kernel initial data
  let start ← IO.monoNanosNow
  let final ← repeatKernel kernel data repetitions initial
  let stop ← IO.monoNanosNow
  IO.println s!"{stop - start} {final.1} {final.2} {warm.1} {warm.2}"

def main (args : List String) : IO Unit := do
  let [kernel, sizeText, repetitionsText, seedText] := args
    | throw <| IO.userError "expected kernel size repetitions seed"
  let size ← parseBoundedNat "size" sizeText maxSize
  let repetitions ← parseBoundedNat "repetitions" repetitionsText maxRepetitions
  checkTimedBytes size repetitions
  let seed ← parseNat "seed" seedText
  bench kernel size repetitions seed

end AdlerTiming

def main (args : List String) : IO Unit := AdlerTiming.main args
