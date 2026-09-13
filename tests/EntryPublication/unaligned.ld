/* Deliberately violate the pass's alignment at final link. Admission must
   reject this executable before installing any runtime entry headers. */
SECTIONS {
  .stabilizer_bad_alignment ALIGN(8) : SUBALIGN(1) {
    BYTE(0x90)
    *(.stabilizer_bad_alignment)
  }
}
INSERT AFTER .text;
