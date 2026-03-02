# Gwyddion Codebase Notes

> Derived from analysis of the full Gwyddion codebase (v2.66, SVN r26369, 2024-05-30).

---

## What is Gwyddion?

Gwyddion is a **Scanning Probe Microscopy (SPM) data visualization and analysis toolkit**
written in C with GTK+2/GLib. It is the most widely used open-source tool for analyzing
AFM, STM, and related microscopy data. It supports Linux/Unix and Windows.

- **Version:** 2.66 (latest in this repo, actively developed; v2.67 in progress)
- **Language:** C (GTK+2, GLib/GObject)
- **License:** GNU General Public License v2 (see `gwyddion/COPYING`)
- **Lines of code:** ~200,000+ across core libraries and 270+ modules

---

## License

**GNU GPL v2** — core application and all libraries.

Exceptions:
- `plugins/` directory — **Public Domain**
- `modules/plugin-proxy.c` — **Special exception**: may be used as format documentation
  without GPL "tainting" of derived works (see the file for details)

---

## Codebase Statistics

| Component | Count |
|---|---|
| File format modules (`modules/file/`) | 140+ C files, ~170 format entries |
| Data processing modules (`modules/process/`) | 130+ modules |
| Interactive tools (`modules/tools/`) | 20+ tools |
| Custom GTK+ widgets (`libgwydgets/`) | 60+ widgets |
| Supported SPM manufacturers | NT-MDT, RHK, Veeco/Bruker, Park Systems, JPK, Omicron, WITec, Zeiss, Shimadzu, Hitachi, … |

---

## Library Dependency Order

```
libgwyddion   →  (none)
libprocess    →  libgwyddion
libdraw       →  libgwyddion + libprocess
libgwydgets   →  libgwyddion + libprocess + libdraw
libgwymodule  →  libgwyddion + libprocess + libdraw + libgwydgets
app/          →  all above
gwyddion/     →  all above + app
modules/      →  all above + app
```

---

## Key Files Quick Reference

| File | Purpose |
|---|---|
| `libgwyddion/gwycontainer.h` | Universal key-value data storage (root data type) |
| `libprocess/datafield.h` | `GwyDataField` — main 2D SPM data type |
| `libprocess/brick.h` | `GwyBrick` — 3D volume data |
| `libprocess/spectra.h` | `GwySpectra` — 1D spectroscopy curves |
| `libgwymodule/gwymodule-file.h` | File module plugin interface |
| `modules/file/*.c` | 140+ format-specific parsers |
| `app/file.c` | File open/save dialog and format detection |
| `app/data-browser.c` | Main data management UI |
| `gwyddion/gwyddion.c` | Application entry point |
| `modules/pygwy/` | Existing Python bindings (PyGTK-based) |
| `modules/pygwy/README.pygwy` | pygwy plugin API documentation |

---

## Data Model (GwyContainer Key Hierarchy)

```
GwyContainer (root)
├── /0/data          → GwyDataField (first channel image)
├── /0/meta/         → Metadata (scan params, instrument info)
├── /0/base/palette  → Color map name (string)
├── /1/data          → GwyDataField (second channel)
├── /1/data/title    → Channel name (string)
└── /filename        → File path (string)
```

---

## File Format Support Summary

See [../file-format-table.md](../file-format-table.md) for the full table.

Quick stats from `user-guide/en/xml/file-format-table.xml`:
- **Read-only formats:** ~130
- **Read+Write formats:** ~25 (GSF, GWY, NRRD, SPIP-ASC, SDF, Igor IBW, Assing AFM, WSxM, OpenEXR, …)
- **Write-only formats:** 4 (VTK, PLY, OFF, ASCII text matrix)
- **Formats with SPS (spectroscopy):** ~20
- **Formats with Volume data:** ~15
- **Formats with Curve maps:** ~10
- **Unfinished/experimental modules [a]:** Ambios AMB, Anfor SIF, Veeco Dimension 3100D, JEOL TEM, Quazar NPIC

---

## Build System

- **Autotools** (autoconf + automake + libtool)
- `./configure && make && make install`
- Optional: `--enable-pygwy` to build Python bindings
- Cross-compilation scripts for Windows (MinGW32) in `mingw32-cross-compile/`
- RPM `.spec` file in `data/gwyddion.spec.in`

---

## Language Bindings & Scripting

| Language | Location | Status |
|---|---|---|
| Python | `modules/pygwy/`, `python/Gwyddion/` | Functional (PyGTK2, legacy) |
| Perl | `perl/Gwyddion/` | Plugin support |
| Ruby | `ruby/` | Plugin support |

---

## Standalone Companion Tools

| Tool | Description |
|---|---|
| `gwybatch/` | Command-line batch processing using Gwyddion algorithms |
| `gwydump/` | Dumps `.gwy` file structure in human-readable text |
| `gwyfract/` | Mandelbrot/Julia renderer using Gwyddion libs (demo app) |
| `gwyiew/` | Minimal SPM viewer (example of standalone app using Gwyddion libs) |
| `threshold-example/` | Example standalone Gwyddion module for developers |
