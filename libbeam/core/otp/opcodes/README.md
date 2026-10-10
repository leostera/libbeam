# Pinned interpreter-generation inputs

`manifest.json` records source paths, checksums and the exact ordered input list
for the unchanged `libbeam/tools/otp/beam_makeops`. These are byte-identical
snapshot transplants; upstream whitespace and license notices are preserved.

`libbeam/tools/generate_opcodes.py` verifies the inputs and generates the full
64-bit interpreter outputs in the build tree. Exact generic/specific/tag metadata,
whole admitted transformation cases, and selected generated instruction bodies
now feed the C loader and executor. `project_transform.py` and
`project_dispatch.py` record the fallible-allocation/ownership/safepoint adaptations.
Missing helpers/cases reject admission rather than gaining stub implementations.
Opcode numbering, masks, signatures, rule order and packing remain generated.

See the historical [loader admission record](../../../../docs/rfds/0003-loader-program.md)
and current [first-execution record](../../../../docs/rfds/0003-first-execution.md)
for provenance, the explicit profile and outstanding breadth.
