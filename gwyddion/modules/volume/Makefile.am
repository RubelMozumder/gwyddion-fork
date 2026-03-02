# $Id: Makefile.am 25846 2023-10-12 19:37:10Z klapetek $

# Put sources that are for some reason disabled yet present in svn checkouts
# here.  This makes them distributed and translations of their messages are
# kept.  If we remove them completely, various bits can get lost.
disabled_sources = volume_xystitch.c

volumedir = $(pkglibdir)/modules/volume

# Only bundle modules without own dependencies.  If something goes wrong with
# any required libraries at run-time, it will at most break these individual
# unbundled modules, not the bundle.
bundlable_modules = \
	volume_1dfft.la \
	volume_addframe.la \
	volume_arithmetic.la \
	volume_asciiexport.la \
	volume_calibrate.la \
	volume_crop.la \
	volume_drift.la \
	volume_equiplane.la \
	volume_extract.la \
	volume_facetlevel.la \
	volume_fdfit.la \
	volume_flattenbase.la \
	volume_invert.la \
	volume_kmeans.la \
	volume_kmedians.la \
	volume_lawnize.la \
	volume_linelevel.la \
	volume_linestat.la \
	volume_localdetect.la \
	volume_meanplanelevel.la \
	volume_mfmrecalc.la \
	volume_outliers.la \
	volume_planelevel.la \
	volume_planestat.la \
	volume_psf.la \
	volume_removeframe.la \
	volume_rephase.la \
	volume_scars.la \
	volume_slice.la \
	volume_stepline.la \
	volume_strayfield.la \
	volume_swaxes.la \
	volume_xyarithmetics.la \
	volume_xystitch.la \
	volume_zcal.la \
	volume_zposlevel.la \
	volumeops.la

# volume_modules is either the bundle or all the individual modules.
if MODULE_BUNDLING
volume_modules = volume.la
BUILT_SOURCES = volume.c
volume_la_CFLAGS = $(AM_CFLAGS) -DGWY_MODULE_BUNDLING
else
volume_modules = $(bundlable_modules)
endif

volume_LTLIBRARIES = \
	$(volume_modules)

EXTRA_DIST = \
	$(disabled_sources)

if OS_MSWIN
no_undefined = -no-undefined
endif

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

AM_CPPFLAGS = -I$(top_srcdir) -DG_LOG_DOMAIN=\"Module\"
AM_CFLAGS = @COMMON_CFLAGS@ @OPENMP_CFLAGS@
AM_LDFLAGS = -avoid-version -module $(no_undefined) $(module_libadd) @OPENMP_CFLAGS@

volume_1dfft_la_SOURCES           = volume_1dfft.c
volume_addframe_la_SOURCES        = volume_addframe.c
volume_arithmetic_la_SOURCES      = volume_arithmetic.c
volume_asciiexport_la_SOURCES     = volume_asciiexport.c
volume_calibrate_la_SOURCES       = volume_calibrate.c
volume_crop_la_SOURCES            = volume_crop.c
volume_drift_la_SOURCES           = volume_drift.c
volume_equiplane_la_SOURCES       = volume_equiplane.c
volume_extract_la_SOURCES         = volume_extract.c
volume_facetlevel_la_SOURCES      = volume_facetlevel.c
volume_fdfit_la_SOURCES           = volume_fdfit.c
volume_flattenbase_la_SOURCES     = volume_flattenbase.c
volume_invert_la_SOURCES          = volume_invert.c
volume_kmeans_la_SOURCES          = volume_kmeans.c
volume_kmedians_la_SOURCES        = volume_kmedians.c
volume_lawnize_la_SOURCES         = volume_lawnize.c
volume_linelevel_la_SOURCES       = volume_linelevel.c
volume_linestat_la_SOURCES        = volume_linestat.c
volume_localdetect_la_SOURCES     = volume_localdetect.c
volume_meanplanelevel_la_SOURCES  = volume_meanplanelevel.c
volume_mfmrecalc_la_SOURCES       = volume_mfmrecalc.c
volume_outliers_la_SOURCES        = volume_outliers.c
volume_planelevel_la_SOURCES      = volume_planelevel.c 
volume_planestat_la_SOURCES       = volume_planestat.c
volume_psf_la_SOURCES             = volume_psf.c
volume_removeframe_la_SOURCES     = volume_removeframe.c
volume_rephase_la_SOURCES         = volume_rephase.c
volume_scars_la_SOURCES           = volume_scars.c
volume_slice_la_SOURCES           = volume_slice.c
volume_stepline_la_SOURCES        = volume_stepline.c   
volume_strayfield_la_SOURCES      = volume_strayfield.c
volume_swaxes_la_SOURCES          = volume_swaxes.c
volume_xyarithmetics_la_SOURCES   = volume_xyarithmetics.c
volume_xystitch_la_SOURCES        = volume_xystitch.c
volume_zcal_la_SOURCES            = volume_zcal.c
volume_zposlevel_la_SOURCES       = volume_zposlevel.c
volumeops_la_SOURCES              = volumeops.c

volume_la_SOURCES = \
	$(volume_1dfft_la_SOURCES) \
	$(volume_addframe_la_SOURCES) \
	$(volume_arithmetic_la_SOURCES) \
	$(volume_asciiexport_la_SOURCES) \
	$(volume_calibrate_la_SOURCES) \
	$(volume_crop_la_SOURCES) \
	$(volume_drift_la_SOURCES) \
	$(volume_equiplane_la_SOURCES) \
	$(volume_extract_la_SOURCES) \
	$(volume_facetlevel_la_SOURCES) \
	$(volume_fdfit_la_SOURCES) \
	$(volume_flattenbase_la_SOURCES) \
	$(volume_invert_la_SOURCES) \
	$(volume_kmeans_la_SOURCES) \
	$(volume_kmedians_la_SOURCES) \
	$(volume_lawnize_la_SOURCES) \
	$(volume_linelevel_la_SOURCES) \
	$(volume_linestat_la_SOURCES) \
	$(volume_localdetect_la_SOURCES) \
	$(volume_meanplanelevel_la_SOURCES) \
	$(volume_mfmrecalc_la_SOURCES) \
	$(volume_outliers_la_SOURCES) \
	$(volume_planelevel_la_SOURCES) \
	$(volume_planestat_la_SOURCES) \
	$(volume_psf_la_SOURCES) \
	$(volume_removeframe_la_SOURCES) \
	$(volume_rephase_la_SOURCES) \
	$(volume_scars_la_SOURCES) \
	$(volume_slice_la_SOURCES) \
	$(volume_stepline_la_SOURCES) \
	$(volume_strayfield_la_SOURCES) \
	$(volume_swaxes_la_SOURCES) \
	$(volume_xyarithmetics_la_SOURCES) \
	$(volume_xystitch_la_SOURCES) \
	$(volume_zcal_la_SOURCES) \
	$(volume_zposlevel_la_SOURCES) \
	$(volumeops_la_SOURCES) \
	volume.c

volume.c: $(srcdir)/Makefile.am $(top_srcdir)/utils/gen-module-bundle.sh
	$(SHELL) $(top_srcdir)/utils/gen-module-bundle.sh volume.c $(bundlable_modules)

CLEAN_FILES = volume.c

clean-local:
	rm -f core.* *~

