# Admitted OTP naming inputs

`atom.names` and `bif.tab` are exact copies of the snapshot inputs recorded in
[RFD 0003's term/atom transplant](../../../docs/rfds/0003-terms-and-atoms.md).
The latter includes prior libbeam profile edits; it is not claimed pristine.

`../../tools/otp/make_tables` is the unchanged OTP generator. The small
`generate_atoms.py` driver verifies input hashes and projects only its atom
outputs into immutable names and compile-time aliases. It preserves ordering;
there is no independently maintained atom-number list.

These declarations do **not** admit any builtin implementation or native effect.
Generated BIF source files are not compiled. Outputs stay in the build tree.
The next opcode-generation cluster is separate and remains outstanding.
