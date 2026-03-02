# $Id: Makefile.am 26316 2024-05-06 09:34:53Z yeti-dn $

SUBDIRS = hdf5

# For each optional module foo there is variable foo_module that should be set
# to foo.la when the module is enabled.
#
# If a module is disabled, add its source files to disabled_sources.  This
# ensures they are still included in tarballs, preserves translations, etc.
disabled_sources = zeissczi.c

# Modules requiring libXML2 and zlib to compile.
if HAVE_XML2
if HAVE_ZLIB
spml_module     = spml.la
spml_la_SOURCES = spml.c
spml_la_CFLAGS  = $(AM_CFLAGS) @XML2_CFLAGS@
spml_la_LIBADD  = @XML2_LIBS@ @ZLIB@
else
spml_module =
disabled_sources += spml.c
endif
else
spml_module =
disabled_sources += spml.c
endif

# Modules requiring libXML2 and some ZIP library to compile.
if HAVE_XML2
if HAVE_ANY_ZIP
apedaxfile_module     = apedaxfile.la
apedaxfile_la_SOURCES = apedaxfile.c gwyzip.h
apedaxfile_la_CFLAGS  = $(AM_CFLAGS) @XML2_CFLAGS@ $(zip_cflags)
apedaxfile_la_LIBADD  = @XML2_LIBS@ $(zip_libs)
else
apedaxfile_module =
disabled_sources += apedaxfile.c
endif
else
apedaxfile_module =
disabled_sources += apedaxfile.c
endif

# Modules requiring libXML2 to compile.
if HAVE_XML2
anasys_xml_module     = anasys_xml.la
anasys_xml_la_SOURCES = anasys_xml.c
anasys_xml_la_CFLAGS  = $(AM_CFLAGS) @XML2_CFLAGS@
anasys_xml_la_LIBADD  = @XML2_LIBS@
else
anasys_xml_module =
disabled_sources += anasys_xml.c
endif

# Modules requiring zlib to compile.
if HAVE_ZLIB
matfile_module     = matfile.la
matfile_la_SOURCES = matfile.c gwyzlib.h
matfile_la_LIBADD  = @ZLIB@
else
matfile_module =
disabled_sources += matfile.c
endif

# Modules requiring some ZIP library to compile.
if HAVE_ANY_ZIP
nanoobserver_module     = nanoobserver.la
nanoobserver_la_SOURCES = nanoobserver.c gwyzip.h
nanoobserver_la_CFLAGS  = $(AM_CFLAGS) $(zip_cflags)
nanoobserver_la_LIBADD  = $(zip_libs)

nanoscantech_module     = nanoscantech.la
nanoscantech_la_SOURCES = nanoscantech.c gwyzip.h
nanoscantech_la_CFLAGS  = $(AM_CFLAGS) $(zip_cflags)
nanoscantech_la_LIBADD  = $(zip_libs)

opengps_module     = opengps.la
opengps_la_SOURCES = opengps.c gwyzip.h
opengps_la_CFLAGS  = $(AM_CFLAGS) $(zip_cflags)
opengps_la_LIBADD  = $(zip_libs)

scnxfile_module     = scnxfile.la
scnxfile_la_SOURCES = scnxfile.c gwyzip.h
scnxfile_la_CFLAGS  = $(AM_CFLAGS) $(zip_cflags)
scnxfile_la_LIBADD  = $(zip_libs)

sensofarx_module     = sensofarx.la
sensofarx_la_SOURCES = sensofarx.c
sensofarx_la_CFLAGS  = $(AM_CFLAGS) $(zip_cflags)
sensofarx_la_LIBADD  = $(zip_libs)

spmxfile_module     = spmxfile.la
spmxfile_la_SOURCES = spmxfile.c gwyzip.h
spmxfile_la_CFLAGS  = $(AM_CFLAGS) $(zip_cflags)
spmxfile_la_LIBADD  = $(zip_libs)

zonfile_module     = zonfile.la
zonfile_la_SOURCES = zonfile.c gwyzip.h
zonfile_la_CFLAGS  = $(AM_CFLAGS) $(zip_cflags)
zonfile_la_LIBADD  = $(zip_libs)
else
nanoobserver_module =
nanoscantech_module =
opengps_module =
scnxfile_module =
sensofarx_module =
spmxfile_module =
zonfile_module =
disabled_sources += nanoobserver.c nanoscantech.c opengps.c scnxfile.c sensofarx.c spmxfile.c zonfile.c
endif

# Modules requiring cfitsio to compile.
if HAVE_CFITSIO
fitsfile_module     = fitsfile.la
fitsfile_la_SOURCES = fitsfile.c
fitsfile_la_CFLAGS  = $(AM_CFLAGS) @CFITSIO_CFLAGS@
fitsfile_la_LIBADD  = @CFITSIO_LIBS@
else
fitsfile_module =
disabled_sources += fitsfile.c
endif

# Modules requiring jansson to compile.
if HAVE_JANSSON
psppt_module     = psppt.la
psppt_la_SOURCES = psppt.c
psppt_la_CFLAGS  = $(AM_CFLAGS) @JANSSON_CFLAGS@
psppt_la_LIBADD  = @JANSSON_LIBS@
else
psppt_module =
disabled_sources += psppt.c
endif

# Modules requiring C++ to compile.
if HAVE_CXX
hdrimage_module     = hdrimage.la
hdrimage_la_SOURCES = hdrimage.cc gwytiff.c gwytiff.h image-keys.h
else
hdrimage_module =
disabled_sources += hdrimage.cc
endif

# Configure should ensure at most one of the following is enabled.
if HAVE_LIBZIP
zip_cflags = @LIBZIP_CFLAGS@
zip_libs = @LIBZIP_LIBS@
endif

if HAVE_MINIZIP1
zip_cflags = @MINIZIP1_CFLAGS@
zip_libs = @MINIZIP1_LIBS@
endif

if HAVE_MINIZIP2
zip_cflags = @MINIZIP2_CFLAGS@
zip_libs = @MINIZIP2_LIBS@
endif

if HAVE_ZZIP
zip_cflags = @ZZIP_CFLAGS@
zip_libs = @ZZIP_LIBS@
endif

# These modules compile also without the respective libraries so just add the
# flags if the libraries are available.
createc_la_LIBADD    = @ZLIB@
hdrimage_la_LIBADD   = @EXR_LIBS@ @PNG_LIBS@
hdrimage_la_CXXFLAGS = $(AM_CXXFLAGS) @PNG_CFLAGS@ @EXR_CFLAGS@ @HDRIMAGE_EXTRA_CFLAGS@
imgexport_la_LIBADD  = @PNG_LIBS@ @WEBP_LIBS@
imgexport_la_CFLAGS  = $(AM_CFLAGS) @PNG_CFLAGS@ @WEBP_CFLAGS@
jpkscan_la_CFLAGS    = $(AM_CFLAGS) $(zip_cflags)
jpkscan_la_LIBADD    = $(zip_libs)
keyence_la_CFLAGS    = $(AM_CFLAGS) $(zip_cflags)
keyence_la_LIBADD    = $(zip_libs)
npyfile_la_CFLAGS    = $(AM_CFLAGS) $(zip_cflags)
npyfile_la_LIBADD    = $(zip_libs)
nrrdfile_la_LIBADD   = @ZLIB@ @BZIP2@
oirfile_la_CFLAGS    = $(AM_CFLAGS) $(zip_cflags)
oirfile_la_LIBADD    = $(zip_libs)
pixmap_la_LIBADD     = @PNG_LIBS@
pixmap_la_CFLAGS     = $(AM_CFLAGS) @PNG_CFLAGS@
rhk_sm4_la_LIBADD    = @ZLIB@
surffile_la_LIBADD   = @ZLIB@

filedir = $(pkglibdir)/modules/file

# Only bundle modules without their own dependencies.  If something goes wrong
# with any required libraries at run-time, it will at most break these
# individual unbundled modules, not the bundle.
bundlable_modules = \
	accurexii-txt.la \
	afmw-spec.la \
	aistfile.la \
	alicona.la \
	ambfile.la \
	ambprofile.la \
	andorsif.la \
	anfatec.la \
	apefile.la \
	ardf.la \
	asciiexport.la \
	asdfile.la \
	assing-afm.la \
	attocube.la \
	bcrfile.la \
	burleigh.la \
	burleigh_bii.la \
	burleigh_exp.la \
	codevfile.la \
	csmfile.la \
	dektakvca.la \
	dektakxml.la \
	dimensionfile.la \
	dmefile.la \
	dm3file.la \
	dumbfile.la \
	ecsfile.la \
	evovisxml.la \
	ezdfile.la \
	femtoscan.la \
	femtoscan-txt.la \
	formats3d.la \
	gdeffile.la \
	gsffile.la \
	gwyfile.la \
	gxyzffile.la \
	hdf4file.la \
	hitachi-afm.la \
	hitachi-sem.la \
	igorfile.la \
	intelliwave.la \
	intematix.la \
	iso28600.la \
	jeol.la \
	jeoltem.la \
	jspmfile.la \
	leica.la \
	lextfile.la \
	magellan.la \
	mapvue.la \
	metropro.la \
	microprof.la \
	miffile.la \
	mifile.la \
	mulfile.la \
	nanoeducator.la \
	nanomagnetics.la \
	nanonics.la \
	nanonis.la \
	nanonis-spec.la \
	nanoscan.la \
	nanoscope-ii.la \
	nanoscope.la \
	nanosystemz.la \
	nanotop.la \
	netcdf.la \
	nmmxyz.la \
	nova-asc.la \
	nt-mdt.la \
	nxiifile.la \
	oldmda.la \
	ols.la \
	ometiff.la \
	omicron.la \
	omicronflat.la \
	omicronmatrix.la \
	opdfile.la \
	phoenix.la \
	pnifile.la \
	pltfile.la \
	princetonspe.la \
	psia.la \
	pt3file.la \
	quazarnpic.la \
	quesant.la \
	rawfile.la \
	rawgraph.la \
	renishaw.la \
	rhk-sm3.la \
	rhk-spm32.la \
	robotics.la \
	s94file.la \
	scafile.la \
	scanfile.la \
	sdfile.la \
	seiko.la \
	sensofar.la \
	sensolytics.la \
	shimadzu.la \
	sicmfile.la \
	sis.la \
	spcfile.la \
	spip-asc.la \
	spmlab.la \
	spmlabf.la \
	stmprg.la \
	stpfile.la \
	tescan.la \
	tiaser.la \
	ttffile.la \
	unisoku.la \
	win_stm.la \
	wipfile.la \
	witec-asc.la \
	witfile.la \
	wsffile.la \
	wrustfile.la \
	wsxmfile.la \
	xyzexport.la \
	zeiss.la \
	zeisslsm.la \
	zemax.la \
	zmgfile.la

# file_modules is either the bundle or all the individual modules.
if MODULE_BUNDLING
file_modules = file.la
BUILT_SOURCES = file.c
file_la_CFLAGS = $(AM_CFLAGS) -DGWY_MODULE_BUNDLING
gwytiff_unbundled =
gwytiff_bundled = gwytiff.c
else
file_modules = $(bundlable_modules)
gwytiff_unbundled = gwytiff.c
gwytiff_bundled =
endif

# Modules listed verbatim use external libraries optionally. So we always build them but never
# bundle them (for consistency). Those listed indirectly via a variable are built optionally.
file_LTLIBRARIES = \
	$(file_modules) \
	createc.la \
	imgexport.la \
	jpkscan.la \
	keyence.la \
	npyfile.la \
	nrrdfile.la \
	oirfile.la \
	pixmap.la \
	rhk-sm4.la \
	surffile.la \
	$(anasys_xml_module) \
	$(apedaxfile_module) \
	$(fitsfile_module) \
	$(hdrimage_module) \
	$(matfile_module) \
	$(nanoobserver_module) \
	$(nanoscantech_module) \
	$(opengps_module) \
	$(psppt_module) \
	$(sensofarx_module) \
	$(scnxfile_module) \
	$(spml_module) \
	$(spmxfile_module) \
	$(zonfile_module)

noinst_HEADERS = \
	err.h \
	get.h

EXTRA_DIST = \
	$(disabled_sources) \
	1CHECKLIST

if OS_MSWIN
no_undefined = -no-undefined
endif

AM_CPPFLAGS = -I$(top_srcdir) -DG_LOG_DOMAIN=\"Module\"
AM_CFLAGS = @COMMON_CFLAGS@
AM_CXXFLAGS = @COMMON_CXXFLAGS@
AM_LDFLAGS = -avoid-version -module $(no_undefined) $(module_libadd)

if MODULE_DEPENDENCIES
module_libadd = \
	$(top_builddir)/app/libgwyapp2.la \
	$(top_builddir)/libdraw/libgwydraw2.la \
	$(top_builddir)/libgwyddion/libgwyddion2.la \
	$(top_builddir)/libgwydgets/libgwydgets2.la \
	$(top_builddir)/libgwymodule/libgwymodule2.la \
	$(top_builddir)/libprocess/libgwyprocess2.la \
	@GTK_LIBS@ @LIBM@
else
module_libadd = @LIBM@
endif

# Standalone modules (with optional external dependencies)
createc_la_SOURCES       = createc.c gwyzlib.h
imgexport_la_SOURCES     = imgexport.c gwytiff.c gwytiff.h image-keys.h
jpkscan_la_SOURCES       = jpkscan.c jpk.h gwytiff.c gwytiff.h gwyzip.h
keyence_la_SOURCES       = keyence.c gwyzip.h
npyfile_la_SOURCES       = npyfile.c gwyzip.h
nrrdfile_la_SOURCES      = nrrdfile.c
oirfile_la_SOURCES       = oirfile.c gwyzip.h
pixmap_la_SOURCES        = pixmap.c gwytiff.c gwytiff.h image-keys.h
rhk_sm4_la_SOURCES       = rhk-sm4.c gwyzlib.h
surffile_la_SOURCES      = surffile.c gwyzlib.h

# Bundlable modules, must be listed in file_la_SOURCES below.
accurexii_txt_la_SOURCES = accurexii-txt.c
afmw_spec_la_SOURCES     = afmw-spec.c
aistfile_la_SOURCES      = aistfile.c
alicona_la_SOURCES       = alicona.c
ambfile_la_SOURCES       = ambfile.c
ambprofile_la_SOURCES    = ambprofile.c
andorsif_la_SOURCES      = andorsif.c
anfatec_la_SOURCES       = anfatec.c
apefile_la_SOURCES       = apefile.c
ardf_la_SOURCES          = ardf.c
asciiexport_la_SOURCES   = asciiexport.c
asdfile_la_SOURCES       = asdfile.c
assing_afm_la_SOURCES    = assing-afm.c
attocube_la_SOURCES      = attocube.c
bcrfile_la_SOURCES       = bcrfile.c
burleigh_la_SOURCES      = burleigh.c
burleigh_bii_la_SOURCES  = burleigh_bii.c
burleigh_exp_la_SOURCES  = burleigh_exp.c
codevfile_la_SOURCES     = codevfile.c
csmfile_la_SOURCES       = csmfile.c
dektakvca_la_SOURCES     = dektakvca.c
dektakxml_la_SOURCES     = dektakxml.c
dimensionfile_la_SOURCES = dimensionfile.c
dmefile_la_SOURCES       = dmefile.c
dm3file_la_SOURCES       = dm3file.c
dumbfile_la_SOURCES      = dumbfile.c
ecsfile_la_SOURCES       = ecsfile.c
evovisxml_la_SOURCES     = evovisxml.c
ezdfile_la_SOURCES       = ezdfile.c
femtoscan_la_SOURCES     = femtoscan.c
femtoscan_txt_la_SOURCES = femtoscan-txt.c
formats3d_la_SOURCES     = formats3d.c
gdeffile_la_SOURCES      = gdeffile.c
gsffile_la_SOURCES       = gsffile.c
gwyfile_la_SOURCES       = gwyfile.c
gxyzffile_la_SOURCES     = gxyzffile.c
hdf4file_la_SOURCES      = hdf4file.c
hitachi_afm_la_SOURCES   = hitachi-afm.c
hitachi_sem_la_SOURCES   = hitachi-sem.c
igorfile_la_SOURCES      = igorfile.c
intelliwave_la_SOURCES   = intelliwave.c
intematix_la_SOURCES     = intematix.c $(gwytiff_unbundled) gwytiff.h
iso28600_la_SOURCES      = iso28600.c
jeol_la_SOURCES          = jeol.c
jeoltem_la_SOURCES       = jeoltem.c $(gwytiff_unbundled) gwytiff.h
jspmfile_la_SOURCES      = jspmfile.c
leica_la_SOURCES         = leica.c
lextfile_la_SOURCES      = lextfile.c $(gwytiff_unbundled) gwytiff.h
magellan_la_SOURCES      = magellan.c $(gwytiff_unbundled) gwytiff.h
mapvue_la_SOURCES        = mapvue.c
metropro_la_SOURCES      = metropro.c
microprof_la_SOURCES     = microprof.c
miffile_la_SOURCES       = miffile.c
mifile_la_SOURCES        = mifile.c
mulfile_la_SOURCES       = mulfile.c
nanoeducator_la_SOURCES  = nanoeducator.c
nanomagnetics_la_SOURCES = nanomagnetics.c
nanonics_la_SOURCES      = nanonics.c
nanonis_la_SOURCES       = nanonis.c
nanonis_spec_la_SOURCES  = nanonis-spec.c
nanoscan_la_SOURCES      = nanoscan.c
nanoscope_ii_la_SOURCES  = nanoscope-ii.c
nanoscope_la_SOURCES     = nanoscope.c
nanosystemz_LA_SOURCES   = nanosystemz.c
nanotop_la_SOURCES       = nanotop.c
netcdf_la_SOURCES        = netcdf.c
nmmxyz_la_SOURCES        = nmmxyz.c
nova_asc_la_SOURCES      = nova-asc.c
nt_mdt_la_SOURCES        = nt-mdt.c
nxiifile_la_SOURCES      = nxiifile.c
oldmda_la_SOURCES        = oldmda.c
ols_la_SOURCES           = ols.c $(gwytiff_unbundled) gwytiff.h
ometiff_la_SOURCES       = ometiff.c $(gwytiff_unbundled) gwytiff.h
omicron_la_SOURCES       = omicron.c
omicronflat_la_SOURCES   = omicronflat.c
omicronmatrix_la_SOURCES = omicronmatrix.c
opdfile_la_SOURCES       = opdfile.c
phoenix_la_SOURCES       = phoenix.c
pnifile_la_SOURCES       = pnifile.c
pltfile_la_SOURCES       = pltfile.c
princetonspe_la_SOURCES  = princetonspe.c
psia_la_SOURCES          = psia.c $(gwytiff_unbundled) gwytiff.h
pt3file_la_SOURCES       = pt3file.c
quazarnpic_la_SOURCES    = quazarnpic.c
quesant_la_SOURCES       = quesant.c
rawfile_la_SOURCES       = rawfile.c
rawgraph_la_SOURCES      = rawgraph.c
renishaw_la_SOURCES      = renishaw.c
rhk_sm3_la_SOURCES       = rhk-sm3.c
rhk_spm32_la_SOURCES     = rhk-spm32.c
robotics_la_SOURCES      = robotics.c
s94file_la_SOURCES       = s94file.c
scafile_la_SOURCES       = scafile.c
scanfile_la_SOURCES      = scanfile.c
sdfile_la_SOURCES        = sdfile.c
seiko_la_SOURCES         = seiko.c
sensofar_la_SOURCES      = sensofar.c
sensolytics_la_SOURCES   = sensolytics.c
shimadzu_la_SOURCES      = shimadzu.c
sicmfile_la_SOURCES      = sicmfile.c
sis_la_SOURCES           = sis.c
spcfile_la_SOURCES       = spcfile.c
spip_asc_la_SOURCES      = spip-asc.c
spmlab_la_SOURCES        = spmlab.c
spmlabf_la_SOURCES       = spmlabf.c
stmprg_la_SOURCES        = stmprg.c
stpfile_la_SOURCES       = stpfile.c
tescan_la_SOURCES        = tescan.c $(gwytiff_unbundled) gwytiff.h
tiaser_la_SOURCES        = tiaser.c
ttffile_la_SOURCES       = ttffile.c $(gwytiff_unbundled) gwytiff.h
unisoku_la_SOURCES       = unisoku.c
win_stm_la_SOURCES       = win_stm.c
wipfile_la_SOURCES       = wipfile.c
witfile_la_SOURCES       = witfile.c
witec_asc_la_SOURCES     = witec-asc.c
wrustfile_la_SOURCES     = wrustfile.c
wsffile_la_SOURCES       = wsffile.c
wsxmfile_la_SOURCES      = wsxmfile.c
xyzexport_la_SOURCES     = xyzexport.c
zeiss_la_SOURCES         = zeiss.c $(gwytiff_unbundled) gwytiff.h
#zeissczi_la_SOURCES      = zeissczi.c
zeisslsm_la_SOURCES      = zeisslsm.c $(gwytiff_unbundled) gwytiff.h
zemax_la_SOURCES         = zemax.c
zmgfile_la_SOURCES       = zmgfile.c

file_la_SOURCES =  \
	$(accurexii_txt_la_SOURCES) \
	$(afmw_spec_la_SOURCES) \
	$(aistfile_la_SOURCES) \
	$(alicona_la_SOURCES) \
	$(ambfile_la_SOURCES) \
	$(ambprofile_la_SOURCES) \
	$(anfatec_la_SOURCES) \
	$(andorsif_la_SOURCES) \
	$(apefile_la_SOURCES) \
	$(ardf_la_SOURCES) \
	$(asciiexport_la_SOURCES) \
	$(asdfile_la_SOURCES) \
	$(assing_afm_la_SOURCES) \
	$(attocube_la_SOURCES) \
	$(bcrfile_la_SOURCES) \
	$(burleigh_la_SOURCES) \
	$(burleigh_bii_la_SOURCES) \
	$(burleigh_exp_la_SOURCES) \
	$(codevfile_la_SOURCES) \
	$(csmfile_la_SOURCES) \
	$(dektakvca_la_SOURCES) \
	$(dektakxml_la_SOURCES) \
	$(dimensionfile_la_SOURCES) \
	$(dmefile_la_SOURCES) \
	$(dm3file_la_SOURCES) \
	$(dumbfile_la_SOURCES) \
	$(ecsfile_la_SOURCES) \
	$(evovisxml_la_SOURCES) \
	$(ezdfile_la_SOURCES) \
	$(femtoscan_la_SOURCES) \
	$(femtoscan_txt_la_SOURCES) \
	$(formats3d_la_SOURCES) \
	$(gdeffile_la_SOURCES) \
	$(gsffile_la_SOURCES) \
	$(gwyfile_la_SOURCES) \
	$(gxyzffile_la_SOURCES) \
	$(hdf4file_la_SOURCES) \
	$(hitachi_afm_la_SOURCES) \
	$(hitachi_sem_la_SOURCES) \
	$(igorfile_la_SOURCES) \
	$(intelliwave_la_SOURCES) \
	$(intematix_la_SOURCES) \
	$(iso28600_la_SOURCES) \
	$(jeol_la_SOURCES) \
	$(jeoltem_la_SOURCES) \
	$(jspmfile_la_SOURCES) \
	$(leica_la_SOURCES) \
	$(lextfile_la_SOURCES) \
	$(magellan_la_SOURCES) \
	$(mapvue_la_SOURCES) \
	$(metropro_la_SOURCES) \
	$(microprof_la_SOURCES) \
	$(miffile_la_SOURCES) \
	$(mifile_la_SOURCES) \
	$(mulfile_la_SOURCES) \
	$(nanoeducator_la_SOURCES) \
	$(nanomagnetics_la_SOURCES) \
	$(nanonics_la_SOURCES) \
	$(nanonis_la_SOURCES) \
	$(nanonis_spec_la_SOURCES) \
	$(nanoscan_la_SOURCES) \
	$(nanoscope_ii_la_SOURCES) \
	$(nanoscope_la_SOURCES) \
	$(nanosystemz_LA_SOURCES) \
	$(nanotop_la_SOURCES) \
	$(netcdf_la_SOURCES) \
	$(nmmxyz_la_SOURCES) \
	$(nova_asc_la_SOURCES) \
	$(nt_mdt_la_SOURCES) \
	$(nxiifile_la_SOURCES) \
	$(oldmda_la_SOURCES) \
	$(ols_la_SOURCES) \
	$(ometiff_la_SOURCES) \
	$(omicron_la_SOURCES) \
	$(omicronflat_la_SOURCES) \
	$(omicronmatrix_la_SOURCES) \
	$(opdfile_la_SOURCES) \
	$(phoenix_la_SOURCES) \
	$(pnifile_la_SOURCES) \
	$(pltfile_la_SOURCES) \
	$(princetonspe_la_SOURCES) \
	$(psia_la_SOURCES) \
	$(pt3file_la_SOURCES) \
	$(quazarnpic_la_SOURCES) \
	$(quesant_la_SOURCES) \
	$(rawfile_la_SOURCES) \
	$(rawgraph_la_SOURCES) \
	$(renishaw_la_SOURCES) \
	$(rhk_sm3_la_SOURCES) \
	$(rhk_spm32_la_SOURCES) \
	$(robotics_la_SOURCES) \
	$(s94file_la_SOURCES) \
	$(scafile_la_SOURCES) \
	$(scanfile_la_SOURCES) \
	$(sdfile_la_SOURCES) \
	$(seiko_la_SOURCES) \
	$(sensofar_la_SOURCES) \
	$(sensolytics_la_SOURCES) \
	$(shimadzu_la_SOURCES) \
	$(sicmfile_la_SOURCES) \
	$(sis_la_SOURCES) \
	$(spcfile_la_SOURCES) \
	$(spip_asc_la_SOURCES) \
	$(spmlab_la_SOURCES) \
	$(spmlabf_la_SOURCES) \
	$(stmprg_la_SOURCES) \
	$(stpfile_la_SOURCES) \
	$(tescan_la_SOURCES) \
	$(tiaser_la_SOURCES) \
	$(ttffile_la_SOURCES) \
	$(unisoku_la_SOURCES) \
	$(win_stm_la_SOURCES) \
	$(wipfile_la_SOURCES) \
	$(witfile_la_SOURCES) \
	$(witec_asc_la_SOURCES) \
	$(wrustfile_la_SOURCES) \
	$(wsffile_la_SOURCES) \
	$(wsxmfile_la_SOURCES) \
	$(xyzexport_la_SOURCES) \
	$(zeiss_la_SOURCES) \
	$(zeisslsm_la_SOURCES) \
	$(zemax_la_SOURCES) \
	$(zmgfile_la_SOURCES) \
	$(gwytiff_bundled) \
	file.c

file.c: $(srcdir)/Makefile.am $(top_builddir)/config.status $(top_srcdir)/utils/gen-module-bundle.sh
	$(SHELL) $(top_srcdir)/utils/gen-module-bundle.sh file.c $(bundlable_modules)

CLEAN_FILES = file.c

clean-local:
	rm -f core.* *~
