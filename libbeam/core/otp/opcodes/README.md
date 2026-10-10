# Pinned interpreter-generation inputs

`manifest.json` records source paths, checksums and the exact ordered input list
for the unchanged `libbeam/tools/otp/beam_makeops`. These are byte-identical
snapshot transplants; upstream whitespace and license notices are preserved.

`libbeam/tools/generate_opcodes.py` verifies the inputs and generates the full
64-bit interpreter tables in the build tree. Its narrow projection extracts only
the exact generic/specific/tag metadata into the compiled C core. It does not
renumber opcodes, rewrite transformation rules or compile unowned ERTS helpers.

See [the loader admission record](../../../../docs/rfds/0003-loader-program.md)
for implemented scope, source provenance and the remaining execution boundary.
