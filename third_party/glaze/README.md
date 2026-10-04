# Glaze

Minimal JSON read/write headers for Glaze 9.0.0, used by `lyricsmpris` and
available for simple configuration files.

- Upstream: https://github.com/stephenberry/glaze
- Version: `v9.0.0`
- Commit: `d78832c82289c61a9315bfbc35332cec9f4e93ca`
- Source archive: https://codeload.github.com/stephenberry/glaze/tar.gz/d78832c82289c61a9315bfbc35332cec9f4e93ca
- Archive SHA-256: `72e88f46a96e3cd014fb26af644c540a11811407fb1332fb0a4e068ceec5ba3f`
- License: MIT, retained in `LICENSE` and the upstream headers.

Only these entry headers, their transitive dependencies, and `LICENSE` are
copied from upstream. All retained upstream files are unmodified:

```cpp
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
```

This retains 72 of the original 258 headers. Supported features include JSON
strings and file I/O, structs, maps, scalar values, arrays/vectors, variants,
custom field metadata, formatted output, and error reporting. Optional features
such as networking, RPC, other serialization formats, generic JSON trees, and
JSON Schema are omitted. Do not include `glaze/glaze.hpp` or `glaze/json.hpp`.

Some shared internal files (including `beve/header.hpp` and SIMD helpers) remain
because the JSON implementation includes them. Dependencies in conditional
compiler/architecture branches are also retained.

The project uses its own CMake interface target for these C++23 headers; it does
not run Glaze's upstream CMake project or require a system installation.

To reproduce this subset from the pinned source archive:

```sh
python3 tools/vendor-glaze.py /path/to/glaze-source.tar.gz
```

The script verifies the archive checksum, copies the JSON dependency closure,
and removes unrelated headers. To update Glaze, update the pinned checksum in
the script and this version record, run it against the new archive, then run
the lyricsmpris tests and check configuration JSON read/write support.
