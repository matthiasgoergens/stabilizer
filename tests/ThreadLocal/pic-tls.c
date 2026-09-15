/* Canonical TLS resolution is outlined into fixed code. GNU ld may relax its
 * TLS instructions without making the caller's relocated body ambiguous. */
_Thread_local int pic_value = 7;
__attribute__((noinline)) int *pic_address(void) { return &pic_value; }
int main(void) { return *pic_address() != 7; }
