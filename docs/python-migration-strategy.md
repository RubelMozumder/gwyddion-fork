# Python Migration Strategy for Gwyddion

> Analysis produced from reviewing the full Gwyddion codebase (v2.66, SVN r26369).

---

## Context

Gwyddion is a ~200,000-line C codebase built on GTK+2/GLib. It already has partial
Python support via **pygwy** (`modules/pygwy/`) — Python bindings using PyGTK/PyGObject.
Any migration strategy should treat pygwy as the existing contract/reference for what
the C libraries expose to Python.

---

## Option 1: Automated C-to-Python Transpilation

**Tools:** LLM-assisted translation, `pycparser`, AST tools

**Why it fails at this scale:**
- GLib's GObject system (`g_type_register_static`, signals, ref counting) has no direct Python equivalent
- `#define` macros like `GWY_MODULE_QUERY2` are untranslatable without semantic understanding
- Binary format parsers with tight C struct/pointer arithmetic produce unreadable output
- Result is C code written in Python syntax, not idiomatic Python

**Verdict:** ❌ Not viable for this codebase.

---

## Option 2: Python Bindings / Wrapping Existing C Libraries

**Tools:** `ctypes`, `cffi`, `SWIG`, `Cython`, `pybind11`, or extending pygwy

**Concept:** Keep the compiled C libraries, expose them through a Python API.

**Pros:**
- Immediately usable — all 200k lines of battle-tested C accessible from Python
- pygwy already bootstraps this for libgwyddion and libprocess
- Zero risk of algorithmic bugs

**Cons:**
- Not truly "native Python" — requires C compilation + shared libraries
- pygwy is built on deprecated PyGTK2 stack; needs rewrite to PyGObject3 / GObject Introspection
- No pure `pip install`

**Verdict:** ✅ Best short-term pragmatic choice.

---

## Option 3: Full Manual Rewrite Using the Python Scientific Stack

**Component mapping:**

| Gwyddion C Component | Python Equivalent |
|---|---|
| `GwyContainer` | Python `dict` / `dataclasses` |
| `GwyDataField` (2D arrays) | `numpy.ndarray` (2D) |
| `GwyBrick` (3D volumes) | `numpy.ndarray` (3D) |
| `GwySpectra` / `GwyDataLine` | `numpy.ndarray` / `pandas.Series` |
| `GwySIUnit` | `pint` library |
| `libprocess` algorithms | `numpy`, `scipy`, `scikit-image` |
| FFT / wavelet | `numpy.fft`, `scipy.signal`, `PyWavelets` |
| Grain analysis / watershed | `scipy.ndimage`, `scikit-image` |
| `libdraw` (colormaps) | `matplotlib`, `Pillow` |
| `libgwydgets` (widgets) | `PyQt6` / `PySide6` |
| `libgwymodule` (plugin system) | Python `importlib` / `pluggy` |
| `modules/file/*.c` (parsers) | Pure Python (`struct`, `h5py`, `xml`) |
| GTK+ main window | `PyQt6` / `PySide6` |
| Serialization (.gwy format) | `pickle`, `h5py`, or custom |

**Pros:** Truly native Python, pip-installable, Jupyter-ready, community-friendly

**Cons:** Enormous scope (years), high risk of subtle numerical bugs, 140+ binary format parsers

**Verdict:** ✅ Best long-term option — requires discipline and phased strategy.

---

## Option 4: Cython Hybrid

**Concept:** Rewrite performance-critical algorithms in Cython; pure Python for logic/UI.

**Assessment:** Useful for specific hotspots (libprocess inner loops). Not ideal as a
primary strategy since SciPy/NumPy already provides comparable performance for most
SPM processing operations.

**Verdict:** ⚠️ Supplement, not primary strategy.

---

## Option 5: Incremental Layer-by-Layer Port (Recommended)

**Concept:** Combine Options 2 and 3 — start with bindings for immediate access, then
progressively replace each layer with native Python, bottom-up.

### Phased Roadmap

```
Phase 1 (Months 1–3): Core Data Model
  - Rewrite libgwyddion → Pure Python
    - GwyContainer → Python dict/dataclass
    - GwySIUnit → pint
  - Rewrite GwyDataField, GwyBrick, GwySpectra → NumPy-backed classes
  - Use pygwy as validation reference for every method

Phase 2 (Months 4–9): Processing Engine
  - Port libprocess → NumPy/SciPy (~75 algorithm files)
  - Validate each function output against C output using real SPM test data
  - Map to scipy functions where coverage exists

Phase 3 (Months 9–18): File Format Parsers
  - Port modules/file/*.c → Pure Python parsers (~140+ formats)
  - Priority: most-used formats first (Nanoscope, NT-MDT, GSF, HDF5, JPK)
  - Use Python `struct` module for binary parsing
  - Use `h5py` for HDF5-based formats (Ergo, Lucent, DATX, NSID)

Phase 4 (Months 18–24+): UI Layer
  - Replace GTK+/libgwydgets → PyQt6/PySide6
  - Replace app/ layer with Qt-based UI
  - Keep same user workflows

Phase 5: Plugin System
  - Replace libgwymodule → Python importlib + pluggy
  - All existing pygwy scripts become first-class citizens
```

### Key Principles

1. **Start bottom-up** — port `libgwyddion` core types first (smallest, highest leverage)
2. **Keep C running as reference** — validate every ported function with real SPM data files
3. **Use pygwy as the contract** — it documents exactly what each C function exposes
4. **Prioritize processing core over file parsers** — libprocess maps cleanly to NumPy/SciPy
5. **Use `h5py` + defined schema as internal format** — modern, Python-native, replaces .gwy
6. **Do NOT port the UI early** — defer until the data model is stable

---

## Comparison Table

| Criteria | Auto-Transpile | Bindings (pygwy++) | Full Rewrite | Cython Hybrid | Incremental Port |
|---|:---:|:---:|:---:|:---:|:---:|
| Time to first working code | Days | Weeks | Years | Months | Weeks/phase |
| Code readability | ❌ | ⚠️ | ✅ | ⚠️ | ✅ |
| Numerical correctness | ❌ | ✅ | ⚠️ | ⚠️ | ✅ |
| Pure Python (pip install) | ⚠️ | ❌ | ✅ | ⚠️ | ✅ (eventually) |
| Long-term maintainability | ❌ | ⚠️ | ✅ | ⚠️ | ✅ |
| Risk of regressions | ❌ | ✅ | ⚠️ | ⚠️ | ✅ |
| Community contribution ease | ❌ | ⚠️ | ✅ | ⚠️ | ✅ |
| **Robust for long support** | ❌ | ⚠️ | ✅ | ⚠️ | ✅✅ |

---

## Existing Python Infrastructure in This Repo

| Path | Description |
|---|---|
| `modules/pygwy/` | Existing Python bindings (PyGTK-based, ~15 files) |
| `modules/pygwy/README.pygwy` | pygwy API and module types documentation |
| `modules/pygwy/gwyutils.py` | Python utility functions |
| `python/Gwyddion/__init__.py` | Python package stub |
| `python/Gwyddion/dump.py` | .gwy file dump utility |

The `pygwy` module supports three plugin types:
- **Process modules** — manipulate GwyDataField via `gwy.data`
- **File modules** — implement `detect_by_name()`, `detect_by_content()`, `load()`, `save()`
- **Volume modules** — manipulate GwyBrick via `gwy.data`
