# Gwyddion Supported File Formats

| File Format | Extensions | Module | Read | Write | SPS | Volume | Curve Map |
|---|---|---|:---:|:---:|:---:|:---:|:---:|
| Accurex II text data | `.txt` | accurexii-txt | Yes | — | — | — | — |
| AFM Workshop spectroscopy | `.csv` | afmw-spec | — | — | Yes | — | — |
| AIST-NT | `.aist` | aistfile | Yes | — | — | — | — |
| Alicona Imaging AL3D data | `.al3d` | alicona | Yes | — | — | — | — |
| Ambios AMB | `.amb` | ambfile | Yes [a] | — | — | — | — |
| Ambios 1D profilometry data | `.dat`, `.xml` | ambprofile | Yes | — | — | — | — |
| Analysis Studio XML | `.axd`, `.axz` | anasys_xml | Yes | — | Yes | — | — |
| Anfor SIF | `.sif` | andorsif | Yes [a] | — | — | — | — |
| Anfatec | `.par`, `.int` | anfatec | Yes | — | — | — | Yes |
| A.P.E. Research DAX | `.dax` | apedaxfile | Yes | — | — | — | — |
| A.P.E. Research APDT | `.apdt` | apedaxfile | Yes | — | — | — | — |
| A.P.E. Research DAT | `.dat` | apefile | Yes | — | — | — | — |
| Asylum Research ARDF | `.ardf` | ardf | Yes | — | — | Yes | — |
| Text matrix of data values | `.txt` | asciiexport | — | Yes | — | — | — |
| Assing AFM | `.afm` | assing-afm | Yes | Yes | — | — | — |
| Attocube Systems ASC | `.asc` | attocube | Yes | — | — | — | — |
| Image Metrology BCR, BCRF | `.bcr`, `.bcrf` | bcrfile | Yes | — | — | — | — |
| Burleigh BII | `.bii` | burleigh_bii | Yes | — | — | — | — |
| Burleigh IMG v2.1 | `.img` | burleigh | Yes | — | — | — | — |
| Burleigh exported data | `.txt`, `.bin` | burleigh_exp | Yes | — | — | — | — |
| Code V interferogram data | `.int` | codevfile | Yes | — | — | — | — |
| Createc DAT | `.dat` | createc | Yes | — | — | — | — |
| Benyuan CSM | `.csm` | csmfile | Yes | — | — | — | — |
| Dektak OPDx profilometry data | `.OPDx` | dektakvca | Yes | — | — | — | — |
| Dektak XML profilometry data | `.xml` | dektakxml | Yes | — | — | — | — |
| Veeco Dimension 3100D | `.001`, `.002`, etc. | dimensionfile | Yes [a] | — | — | — | — |
| Digital Micrograph DM3 TEM data | `.dm3` | dm3file | Yes | — | — | — | — |
| Digital Micrograph DM4 TEM data | `.dm4` | dm3file | Yes | — | — | — | — |
| DME Rasterscope | `.img` | dmefile | Yes | — | — | — | — |
| Gwyddion dumb dump data | `.dump` | dumbfile | Yes | — | — | — | — |
| ECS | `.img` | ecsfile | Yes | — | — | — | — |
| Evovis XML profilometry data | `.xml` | evovisxml | Yes | — | — | — | — |
| Nanosurf EZD, NID | `.ezd`, `.nid` | ezdfile | Yes | — | — | — | — |
| FemtoScan SPM | `.spm` | femtoscan | Yes | — | — | — | — |
| FemtoScan text data | `.txt` | femtoscan-txt | Yes [b] | — | — | — | — |
| Flexible Image Transport System (FITS) | `.fits`, `.fit` | fitsfile | Yes | — | — | — | — |
| VTK structured grid file | `.vtk` | formats3d | — | Yes | — | — | — |
| PLY 3D Polygon File Format | `.ply` | formats3d | — | Yes | — | — | — |
| Wavefront OBJ 3D geometry | `.obj` | formats3d | Yes | Yes | — | — | — |
| Object File Format 3D geometry | `.off` | formats3d | — | Yes | — | — | — |
| Stereolitography STL 3D geometry (binary) | `.stl` | formats3d | Yes | Yes | — | — | — |
| XYZ data | `.xyz`, `.dat` | formats3d | Yes | Yes | — | — | — |
| DME GDEF | `.gdf` | gdeffile | Yes | — | — | — | — |
| Gwyddion Simple Field | `.gsf` | gsffile | Yes | Yes | — | — | — |
| Gwyddion native data | `.gwy` | gwyfile | Yes | Yes | Yes | Yes | — |
| Gwyddion XYZ data | `.gxyzf` | gxyzffile | Yes | Yes | — | — | — |
| Psi HDF4 | `.hdf` | hdf4file | Yes | — | — | — | — |
| Asylum Research Ergo HDF5 | `.h5` | hdf5file | Yes | — | — | — | — |
| Shilps Sciences Lucent HDF5 | `.h5` | hdf5file | Yes | — | Yes | Yes | Yes |
| Generic HDF5 files | `.h5` | hdf5file | Yes | — | — | — | — |
| Matlab MAT 7.x files | `.mat` | hdf5file | Yes | — | — | — | — |
| Hitachi AFM | `.afm` | hitachi-afm | Yes | — | — | — | — |
| Hitachi S-3700 and S-4800 SEM data | `.txt` + image | hitachi-sem | Yes | — | — | — | — |
| WaveMetrics IGOR binary wave v5 | `.ibw` | igorfile | Yes | Yes | — | — | — |
| IntelliWave ESD | `.esd` | intelliwave | Yes | — | — | — | — |
| Intematix SDF | `.sdf` | intematix | Yes | — | — | — | — |
| ISO 28600:2011 SPM data transfer format | `.spm` | iso28600 | Yes | Yes | Limited [c] | — | — |
| JEOL | `.tif` | jeol | Yes | — | — | — | — |
| JEOL TEM image | `.tif` | jeoltem | Yes [a] | — | — | — | — |
| JPK Instruments | `.jpk`, `.jpk-qi-image`, `.jpk-force`, `.jpk-force-map`, `.jpk-qi-data` | jpkscan | Yes | — | Yes | — | Yes |
| JEOL JSPM | `.tif` | jspmfile | Yes | — | — | — | — |
| Keyence profilometry VK3, VK4, VK6, VK7 | `.vk3`, `.vk4`, `.vk6`, `.vk7` | keyence | Yes | — | — | — | — |
| Leica LIF Data File | `.lif` | leica | Yes | — | — | Yes | — |
| Olympus LEXT 4000 | `.lext` | lextfile | Yes | — | — | — | — |
| FEI Magellan SEM images | `.tif` | magellan | Yes | — | — | — | — |
| MapVue | `.map` | mapvue | Yes | — | — | — | — |
| Matlab MAT 5 files | `.mat` | matfile | Yes | — | — | — | — |
| Zygo MetroPro DAT | `.dat` | metropro | Yes | — | — | — | — |
| MicroProf TXT | `.txt` | microprof | Yes | — | — | — | — |
| MicroProf FRT | `.frt` | microprof | Yes | — | — | — | — |
| DME MIF | `.mif` | miffile | Yes | — | — | — | — |
| Molecular Imaging MI | `.mi` | mifile | Yes | — | Limited [c] | — | Yes |
| Aarhus MUL | `.mul` | mulfile | Yes | — | — | — | — |
| Nanoeducator | `.mspm`, `.stm`, `.spm` | nanoeducator | Yes | — | Yes | — | — |
| Nanomagnetics NMI | `.nmi` | nanomagnetics | Yes | — | — | — | — |
| Nanonics NAN | `.nan` | nanonics | Yes | — | — | — | — |
| Nanonis SXM | `.sxm` | nanonis | Yes | — | — | — | — |
| Nanonis STS spectroscopy | `.dat` | nanonis-spec | — | — | Yes | — | — |
| Nano-Solution/NanoObserver | `.nao` | nanoobserver | Yes | — | Yes | — | — |
| Nanoscan XML | `.xml` | nanoscan | Yes | — | — | — | — |
| NanoScanTech | `.nstdat` | nanoscantech | Yes | — | Yes | Yes | — |
| Veeco Nanoscope III | `.spm`, `.001`, `.002`, etc. | nanoscope | Yes | — | Limited [c] | Yes | Yes |
| Veeco Nanoscope II | `.001`, `.002`, etc. | nanoscope-ii | Yes | — | — | — | — |
| NanoSystem profilometry | `.spm` | nanosystemz | Yes | — | — | — | — |
| Nanotop SPM | `.spm` | nanotop | Yes | — | — | — | — |
| GSXM NetCDF | `.nc` | netcdf | Yes | — | — | — | — |
| Nano Measuring Machine profile data | `.dsc` + `.dat` | nmmxyz | Yes [d] | — | — | — | — |
| Nova ASCII | `.txt` | nova-asc | Yes | — | — | — | — |
| Numpy binary serialised array | `.npy` | npyfile | Yes | — | — | — | — |
| Multiple numpy binary serialised arrays | `.npz` | npyfile | Yes | — | — | — | — |
| Nearly raw raster data (NRRD) | `.nrrd` | nrrdfile | Yes [e] | Yes [f] | — | Yes | — |
| NT-MDT | `.mdt` | nt-mdt | Yes | — | Yes | Yes | — |
| EM4SYS NX II | `.bmp` | nxiifile | Yes | — | — | — | — |
| Olympus OIR | `.oir` | oirfile | Yes | — | — | — | — |
| Olympus packed OIR | `.poir` | oirfile | Yes | — | — | — | — |
| NT-MDT old MDA | `.sxml` + `.dat` | oldmda | — | — | — | Yes | — |
| Olympus LEXT 3000 | `.ols` | ols | Yes | — | — | — | — |
| Open Microscopy OME TIFF | `.ome.tiff`, `.ome.tif` | ometiff | Yes | — | — | — | — |
| Omicron SCALA | `.par` + `.tf*`, `.tb*`, `.sf*`, `.sb*` | omicron | Yes | — | Yes | — | — |
| Omicron flat format | `.*_flat` | omicronflat | Yes | — | — | — | — |
| Omicron MATRIX | `.mtrx` | omicronmatrix | Yes | — | Limited [c] | Yes | — |
| Wyko OPD | `.opd` | opdfile | Yes | — | — | — | — |
| Wyko ASCII | `.asc` | opdfile | Yes | — | — | — | — |
| OpenGPS X3P (ISO 5436-2) | `.x3p` | opengps | Yes | — | — | — | — |
| NASA Phoenix Mars mission AFM data | `.dat`, `.lbl` + `.tab` | phoenix | Yes | — | — | — | — |
| Pixmap images | `.png`, `.jpeg`, `.tiff`, `.tga`, `.pnm`, `.bmp` | pixmap | Yes [g] | Yes [h] | — | — | — |
| Nanosurf PLT | `.plt` | pltfile | Yes | — | — | — | — |
| Pacific Nanotechnology PNI | `.pni` | pnifile | Yes | — | — | — | — |
| Princeton Instruments camera SPE | `.spe` | princetonspe | Yes | — | — | — | — |
| Park Systems | `.tiff`, `.tif` | psia | Yes | — | — | — | — |
| Park Systems PS-PPT | `.ps-ppt` | psppt | Yes | — | — | — | Yes |
| SymPhoTime TTTR v2.0 data | `.pt3` | pt3file | Yes | — | — | — | — |
| Quazar NPIC | `.npic` | quazarnpic | Yes [a] | — | — | — | — |
| Quesant AFM | `.afm` | quesant | Yes | — | — | — | — |
| Raw text files | any | rawfile | Yes | — | — | — | — |
| Raw binary files | any | rawfile | Yes | — | — | — | — |
| Graph text data (raw) | any | rawgraph | Yes [i] | — | — | — | — |
| Renishaw WiRE Data File | `.wdf` | renishaw | Yes | — | Yes | Yes | — |
| RHK Instruments SM3 | `.sm3` | rhk-sm3 | Yes | — | Limited [c] | — | — |
| RHK Instruments SM4 | `.sm4` | rhk-sm4 | Yes | — | Limited [c] | — | — |
| RHK Instruments SM2 | `.sm2` | rhk-spm32 | Yes | — | Limited [c] | — | — |
| Automation and Robotics Dual Lens Mapper | `.mcr`, `.mct`, `.mce` | robotics | Yes | — | — | — | — |
| S94 STM files | `.s94` | s94file | Yes | — | — | — | — |
| Surfstand Surface Data File | `.sdf` | sdfile | Yes | Yes | — | — | — |
| Micromap SDFA | `.sdfa` | sdfile | Yes | — | — | — | — |
| Seiko SII | `.xqb`, `.xqd`, `.xqt`, `.xqp`, `.xqj`, `.xqi` | seiko | Yes | — | — | — | — |
| Sensofar PLu | `.plu`, `.apx` | sensofar | Yes | — | — | — | — |
| Sensofar PLUx data | `.plux` | sensofarx | Yes | — | — | — | — |
| Sensolytics DAT | `.dat` | sensolytics | Yes | — | — | — | Yes |
| Shimadzu | `.sph`, `.spp`, `.001`, `.002`, etc. | shimadzu | Yes | — | — | — | — |
| Shimadzu ASCII | `.txt` | shimadzu | Yes | — | — | — | — |
| IonScope SICM | `.img` | sicmfile | Yes | — | — | — | — |
| Surface Imaging Systems | `.sis` | sis | Yes | — | — | — | — |
| Thermo Fisher SPC File | `.spc` | spcfile | — | — | Yes | — | — |
| SPIP ASCII | `.asc` | spip-asc | Yes | Yes | — | — | — |
| Thermicroscopes SPMLab R4-R7 | `.tfr`, `.ffr`, etc. | spmlab | Yes | — | — | — | — |
| Thermicroscopes SPMLab floating point | `.flt` | spmlabf | Yes | — | — | — | — |
| SPML (Scanning Probe Microscopy Markup Language) | `.xml` | spml | Yes | — | — | — | — |
| ATC SPMxFormat data | `.spm` | spmxfile | Yes | — | — | — | — |
| Omicron STMPRG | `tp*`, `ta*` | stmprg | Yes | — | — | — | — |
| Molecular Imaging STP | `.stp` | stpfile | Yes | — | — | — | — |
| Surf | `.sur` | surffile | Yes | — | — | — | — |
| Tescan MIRA SEM images | `.tif` | tescan | Yes | — | — | — | — |
| Tescan LYRA SEM images | `.hdr` + `.png` | tescan | Yes | — | — | — | — |
| FEI Tecnai imaging and analysis (former Emispec) data | `.ser` | tiaser | Yes | — | Yes | Yes | — |
| Corning Tropel UltraSort topographical data | `.ttf` | ttffile | Yes | — | — | — | — |
| Corning Tropel exported CSV data | `.csv` | ttffile | Yes | — | — | — | — |
| Unisoku | `.hdr` + `.dat` | unisoku | Yes | — | — | — | — |
| WinSTM data | `.stm` | win_stm | Yes | — | — | — | — |
| WITec Project data | `.wip` | wipfile | Yes | — | Yes | Yes | — |
| WITec ASCII export | `.dat` | witec-asc | Yes | — | — | — | — |
| WITec | `.wit` | witfile | Yes | — | — | — | — |
| Department of Nanometrology, WRUST | `.dat` | wrustfile | Yes | — | — | — | — |
| AFM Workshop data | `.wsf` | wsffile | Yes | — | — | — | — |
| Nanotec WSxM | `.tom`, `.top`, `.stp` | wsxmfile | Yes | Yes | — | — | — |
| Nanotec WSxM curves | `.cur` | wsxmfile | Yes | — | — | — | — |
| Carl Zeiss SEM scans | `.tif` | zeiss | Yes | — | — | — | — |
| Carl Zeiss CLSM images | `.lsm` | zeisslsm | Yes | — | — | Yes | — |
| Zemax grid sag data | `.dat` | zemax | Yes | — | — | — | — |
| KLA Zeta profilometry data | `.zmg` | zmgfile | Yes | — | — | — | — |
| Keyence ZON data | `.zon` | zonfile | Yes | — | — | — | — |
| OpenEXR images | `.exr` | hdrimage | Yes | Yes | — | — | — |

---

## Column Descriptions

- **Read** — Import support (loading files into Gwyddion)
- **Write** — Export support (saving files from Gwyddion)
- **SPS** — Single-point spectroscopy data support
- **Volume** — 3D volume/brick data support
- **Curve Map** — Curve map data support

---

## Footnotes

- **[a]** — The import module is unfinished due to the lack of documentation, testing files and/or people willing to help with the testing. If you can help please contact the Gwyddion team.
- **[b]** — Regular sampling in both X and Y direction is assumed.
- **[c]** — Spectra curves are imported as graphs; positional information is lost.
- **[d]** — XYZ data are interpolated to a regular grid upon import.
- **[e]** — Not all variants are implemented.
- **[f]** — Data are exported in a fixed attached native-endian floating point format.
- **[g]** — Import support relies on Gdk-Pixbuf and hence may vary among systems.
- **[h]** — Usually lossy, intended for presentational purposes. 16-bit grayscale export is possible to PNG, TIFF and PNM.
- **[i]** — At present, only simple two-column data, imported as graph curves, are supported.

---

*Source: [`user-guide/en/xml/file-format-table.xml`](user-guide/en/xml/file-format-table.xml) — auto-generated from Gwyddion module source files.*
