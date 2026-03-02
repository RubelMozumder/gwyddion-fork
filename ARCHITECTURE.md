# Gwyddion SPM Software Architecture

## Overview
Gwyddion is a Scanning Probe Microscopy (SPM) data visualization and analysis tool written in C with GTK+ UI. It handles multiple SPM data formats from different experiments (STM, AFM, etc.) with different encoding algorithms.

---

## 1. LAYERED ARCHITECTURE

The codebase follows a **layered, modular architecture** with clear dependency management:

```
┌─────────────────────────────────────────────────────────┐
│  gwyddion/          (Main Application & UI)             │
├─────────────────────────────────────────────────────────┤
│  app/               (Application Library - GTK+ UI)      │
├─────────────────────────────────────────────────────────┤
│  libgwymodule/      (Module Loading & Plugin System)     │
├─────────────────────────────────────────────────────────┤
│  libgwydgets/       (Custom GTK+ Widgets)               │
│  libdraw/           (Drawing & Rendering Library)       │
│  libprocess/        (Data Processing Algorithms)        │
├─────────────────────────────────────────────────────────┤
│  libgwyddion/       (Core Data Structures & Utilities)   │
└─────────────────────────────────────────────────────────┘
```

### Dependency Tree (from README):
```
libgwyddion:        (none)
libprocess:         libgwyddion
libdraw:            libgwyddion + libprocess
libgwydgets:        libgwyddion + libprocess + libdraw
libgwymodule:       libgwyddion + libprocess + libdraw + libgwydgets
app:                libgwyddion + libprocess + libdraw + libgwydgets + libgwymodule
gwyddion:           all libraries
modules:            all libraries
```

---

## 2. CORE LAYER: libgwyddion (Foundation)

**Purpose:** Core data structures, utilities, and foundational types

### Key Components:

#### **GwyContainer** (`gwycontainer.h/c`)
- **Purpose:** Universal data storage container using key-value pairs
- **Structure:** Hash table-based with GLib object system
- **Use:** Holds SPM data, metadata, processing results
- **Key Methods:**
  - `gwy_container_get_value()` - retrieve data by key
  - `gwy_container_set_value()` - store data
  - `gwy_container_foreach()` - iterate through items

#### **Other Core Components:**
- `gwyserialization.h` - Object serialization/deserialization (data save/load)
- `gwymath.h` - Mathematical utilities
- `gwyutils.h` - String/file utilities
- `gwysiunit.h` - SI unit handling (nanometers, volts, etc.)
- `gwyinventory.h` - Resource management (presets, colors)
- `gwyresults.h` - Results aggregation

---

## 3. DATA PROCESSING LAYER: libprocess

**Purpose:** SPM data processing algorithms and core data structures

### Key Data Structures:

#### **GwyDataField** (Main 2D Data Type)
- Represents a 2D array of SPM data (e.g., height map from AFM)
- Properties:
  - Dimensions (xres, yres)
  - Physical dimensions (xreal, yreal)
  - Data array (gdouble *data)
  - SI units
- Used for: Height maps, amplitude, phase, force data, etc.

#### **GwyBrick** (3D Volume Data)
- Represents 3D SPM volume data (e.g., spectroscopy maps)
- Used for: Force spectroscopy maps, tomography data

#### **GwySpectra** (1D Spectroscopy Curves)
- Represents point spectroscopy data
- Used for: I-V curves, force curves, current-distance curves

#### **Processing Functions:**
- Arithmetic operations (add, multiply, divide data fields)
- Filtering (median, Gaussian, morphological)
- Leveling and detrending
- Statistical analysis
- Fourier analysis
- Threshold operations

---

## 4. DRAWING & VISUALIZATION LAYER

### **libdraw**
- Rendering and visualization
- Image creation for display
- Color mapping

### **libgwydgets**
- Custom GTK+ widgets
- Data visualization widgets
- Graph plotting
- 3D visualization

---

## 5. MODULE SYSTEM: libgwymodule

**Purpose:** Plugin/module architecture for extensibility

### Module Types (in `/modules/`):
```
modules/
├── file/          [FILE I/O MODULES - Core Topic!]
├── process/       [Data processing plugins]
├── graph/         [Graph analysis plugins]
├── layer/         [Data visualization layers]
├── tools/         [Interactive tools]
├── cmap/          [Color map resources]
└── volume/        [3D volume processing]
```

### Module Registration Pattern:
```c
// Every module implements this pattern:
static gboolean module_register(void);

GWY_MODULE_QUERY2(module_info, module_name)

static gboolean
module_register(void)
{
    // Register file format, process function, or tool
    gwy_module_register_file(name, func, description);
    return TRUE;
}
```

---

## 6. FILE I/O ARCHITECTURE: modules/file/ [KEY COMPONENT]

**This is where SPM file format parsing happens!**

### Structure:
Each file format has a dedicated module:
```
modules/file/
├── nt-mdt.c           [NT-MDT SPM format]
├── rhk-sm4.c          [RHK Technology SM4]
├── rhk-spm32.c        [RHK Technology SPM32]
├── s94file.c          [S94 STM format]
├── pltfile.c          [PLT format]
├── afmfile.c          [AFM format]
├── aistfile.c         [AIST-NT format]
├── spmlabfile.c       [SPM Lab format]
└── [50+ more formats...]
```

### File Loading Pipeline:

```
User Opens File
    ↓
File Dialog (app/filelist.c)
    ↓
File Browser Detects Format
    ↓
Appropriate File Module Loaded (libgwymodule)
    ↓
Module's load_func() Called
    ↓
File Header Parsing & Validation
    ↓
Raw Data Reading & Decoding
    ↓
GwyDataField/GwyBrick Created
    ↓
Data Stored in GwyContainer
    ↓
UI Updated (app/data-browser.c)
    ↓
User Visualizes Data
```

### Example: NT-MDT Module (nt-mdt.c) - 3900+ lines

**File Structure Parsing:**
```c
typedef enum {
    MDT_FRAME_SCANNED      = 0,      // Height map
    MDT_FRAME_SPECTROSCOPY = 1,      // Spectroscopy curve
    MDT_FRAME_TEXT         = 3,      // Metadata
    MDT_FRAME_MDA          = 106,    // Volume data
} MDTFrameType;

typedef enum {
    MDT_SPM_MODE_CONTACT_CONSTANT_HEIGHT = 0,
    MDT_SPM_MODE_CONTACT_ERROR           = 1,
    MDT_SPM_MODE_CONTACT_TOPOGRAPHY      = 2,
    MDT_SPM_MODE_SEMICONTACT_TOPOGRAPHY  = 6,
    MDT_SPM_MODE_CONSTANT_CURRENT        = 14,
    // ... 20+ more SPM modes
} MDTSPMMode;
```

**Data Encoding Algorithms Handled:**
- Different byte orders (big-endian, little-endian)
- Integer/float data types
- Compression schemes (some formats)
- Unit conversions (raw ADC counts → physical units)

### Module Interface (libgwymodule/gwymodule-file.h):

```c
typedef gboolean (*GwyFileSaveFunc)(GwyContainer *data,
                                     const gchar *filename,
                                     GwyRunType run,
                                     GError **error);

typedef gboolean (*GwyFileLoadFunc)(const gchar *filename,
                                     GwyRunType run,
                                     GError **error);
```

---

## 7. APPLICATION LAYER: app/

**Purpose:** UI, user interaction, data management

### Key Components:

#### **data-browser.c/h**
- Main data management window
- Shows loaded data, spectra, volumes
- Handles data selection and switching
- Controls visibility and display options

#### **file.c/h**
- File open/save dialogs
- Calls appropriate file module
- Error handling

#### **dialog.c/h**
- Generic dialog framework for tools and filters

#### **gwyapp.h**
- Application state management

#### **gwytool.c/h, gwyplaintool.c/h**
- Interactive tool framework
- Handles user interactions (mouse, keyboard)

---

## 8. DATA FLOW EXAMPLE: Loading an AFM File

```
1. User clicks "Open File"
   └─> file.c: file_open_dialog()

2. User selects "sample.afm"
   └─> app detects format from extension/magic bytes

3. Module Loader finds appropriate module
   └─> modules/file/afmfile.c loaded

4. afmfile_load() called
   ├─> Opens file
   ├─> Reads header (format-specific structure)
   ├─> Parses metadata (scan size, scan speed, etc.)
   └─> Reads raw data array

5. Raw Data Decoding (handles encoding algorithm)
   ├─> Convert ADC counts to physical units
   ├─> Apply scaling: voltage = ADC × sensitivity / 65536
   └─> Proper orientation (X, Y, Z axes)

6. GwyDataField Created
   ├─> Allocate data array
   ├─> Set dimensions (xres, yres)
   ├─> Set physical size (xreal, yreal)
   └─> Set SI units (meters, volts, etc.)

7. Data Stored in GwyContainer
   └─> Container key: "/0/data" (first channel)

8. UI Updated
   ├─> Data Browser shows new data
   ├─> Display panel renders height map
   └─> Metadata shown in property panel

9. User analyzes data
   ├─> Apply filters (Process menu)
   ├─> Measure features
   ├─> Export results
```

---

## 9. DATA STRUCTURES AT A GLANCE

### Container Hierarchy:
```
GwyContainer (root)
├── /0/data (GwyDataField - first channel)
├── /0/meta/ (metadata)
├── /0/base/palette (color map)
├── /1/data (GwyDataField - second channel)
└── /filename (string)
```

### Object Relationships:
```
GwyContainer
├── GwyDataField (2D arrays)
├── GwyBrick (3D volumes)
├── GwySpectra (1D curves)
├── GwyCalData (calibration)
└── Metadata (strings, numbers)
```

---

## 10. KEY ARCHITECTURAL PATTERNS

### 1. **Plugin Architecture**
- Modules are dynamically loaded at runtime
- Each module registers itself with the system
- Easy to add support for new file formats

### 2. **Container-Based Data Model**
- Everything stored in `GwyContainer` (key-value store)
- Flexible and extensible
- Enables undo/redo and data persistence

### 3. **GObject System**
- Uses GLib's GObject (C object system)
- Reference counting, signals/slots
- Type safety and introspection

### 4. **Separation of Concerns**
- Core data (libgwyddion)
- Data processing (libprocess)
- UI (app, libgwydgets)
- File I/O (modules/file)

### 5. **SPM-Specific Abstraction**
- Multiple frame types (images, spectra, volumes)
- Multiple measurement modes (AFM, STM, EFM, etc.)
- Unit-aware calculations (SI units)

---

## 11. SUPPORTED SPM FILE FORMATS

Core formats handled:
- **NT-MDT** (.mdt) - Russian SPM format
- **RHK Technology** (.sm4, .sm2, .sm3)
- **Veeco/Bruker** (.001, various)
- **Park Systems** (.xym, .tiff)
- **JPK Instruments** (.jpk-force, .jpk-qi-data)
- **Omicron** (.par, .sxm)
- **WITec** (.dat)
- **AIST-NT** (.asc)
- **Generic** (.txt, .dat, .xyz formats)
- **And many others** (~50+ formats total)

---

## 12. KEY FILES TO UNDERSTAND

| File | Purpose |
|------|---------|
| `libgwyddion/gwycontainer.h` | Data container |
| `libprocess/datafield.h` | Main 2D data type |
| `libgwymodule/gwymodule-file.h` | File module interface |
| `modules/file/*.c` | Format-specific parsers |
| `app/file.c` | File dialog & loading |
| `app/data-browser.c` | Main data management UI |
| `gwyddion/gwyddion.c` | Application entry point |

---

## 13. TYPICAL MODIFICATION POINTS

If you need to:
- **Add new file format:** Create `modules/file/myformat.c`
- **Add processing algorithm:** Add to `libprocess/`
- **Add UI feature:** Modify `app/`
- **Add visualization:** Modify `libgwydgets/` or `libdraw/`
- **Add data type:** Extend `libgwyddion/`

---

## Summary

Gwyddion is a **well-architected C application** with:
- ✅ Clear layering (core → processing → UI)
- ✅ Pluggable module system for file formats
- ✅ Flexible container-based data model
- ✅ Support for diverse SPM data types
- ✅ Handles complex encoding algorithms per format
- ✅ Professional GTK+ GUI
- ✅ Extensible processing pipeline

The architecture makes it easy to understand and extend!
