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
Opcode generation and selected whole execution cases are recorded separately in
`opcodes/` and `execution-sources.json`. `numeric-sources.json` records native
addition; `decimal-sources.json` records resumable native schoolbook decimal
formatting. Their real catalog entries are distinct from generated BIF identities.
`bit-copy-sources.json` records native forward copying used by result transport;
this does not admit `bs_create_bin` or any additional BIF.
`bit-writer-sources.json` records the native integer/Unicode segment writers and
their checked caller-owned buffer boundary. They remain dependencies, not an
admitted construction instruction.
