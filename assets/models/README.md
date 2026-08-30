# NAM models

Vendored `.nam` (A1) files and their generated weight headers.
Regenerate a header with:

    python3 external/MicroNAM/nam_to_header.py assets/models/<Model>.nam assets/models/<Model>.h

The header's weight count must match the MicroNAM net it is loaded into:
StandardNet 13802 | LiteNet 6554 | FeatherNet 3026 | NanoNet 842.

- BassModel: standard architecture (13802) — StandardNet
- MarshallModel: feather architecture (3026) — FeatherNet
