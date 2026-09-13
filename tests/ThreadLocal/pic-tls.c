/* PIC lowering emits TLSGD; GNU ld may leave that relocation label after
 * rewriting its instructions. Admission must reject it before publication. */
_Thread_local int pic_value = 7;
__attribute__((noinline)) int *pic_address(void) { return &pic_value; }
int main(void) { return *pic_address() != 7; }
