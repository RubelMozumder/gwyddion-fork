# $Id: Makefile.am 25070 2022-10-06 15:01:28Z yeti-dn $

# Put sources that are for some reason disabled yet present in svn checkouts
# here.  This makes them distributed and translations of their messages are
# kept.  If we remove them completely, various bits can get lost.
disabled_sources =

cmapdir = $(pkglibdir)/modules/cmap

# Only bundle modules without own dependencies.  If something goes wrong with
# any required libraries at run-time, it will at most break these individual
# unbundled modules, not the bundle.
bundlable_modules = \
	cmap_align.la \
	cmap_basicops.la \
	cmap_crop.la \
	cmap_cutter.la \
	cmap_extractcurve.la \
	cmap_fdfit.la \
	cmap_fztofd.la \
	cmap_linestat.la \
	cmap_lockin.la \
	cmap_polylevel.la \
	cmap_possearch.la \
	cmap_simplemech.la \
	cmap_sinebg.la

# cmap_modules is either the bundle or all the individual modules.
if MODULE_BUNDLING
cmap_modules = cmap.la
BUILT_SOURCES = cmap.c
cmap_la_CFLAGS = $(AM_CFLAGS) -DGWY_MODULE_BUNDLING
else
cmap_modules = $(bundlable_modules)
endif

cmap_LTLIBRARIES = \
	$(cmap_modules)

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

# Bundlable modules, must be listed in cmap_la_SOURCES below.
cmap_align_la_SOURCES        = cmap_align.c
cmap_basicops_la_SOURCES     = cmap_basicops.c
cmap_crop_la_SOURCES         = cmap_crop.c
cmap_cutter_la_SOURCES       = cmap_cutter.c
cmap_extractcurve_la_SOURCES = cmap_extractcurve.c
cmap_fdfit_la_SOURCES        = cmap_fdfit.c
cmap_fztofd_la_SOURCES       = cmap_fztofd.c
cmap_linestat_la_SOURCES     = cmap_linestat.c
cmap_lockin_la_SOURCES       = cmap_lockin.c
cmap_polylevel_la_SOURCES    = cmap_polylevel.c
cmap_possearch_la_SOURCES    = cmap_possearch.c
cmap_simplemech_la_SOURCES   = cmap_simplemech.c
cmap_sinebg_la_SOURCES       = cmap_sinebg.c

cmap_la_SOURCES = \
	$(cmap_align_la_SOURCES) \
	$(cmap_basicops_la_SOURCES) \
	$(cmap_crop_la_SOURCES) \
	$(cmap_cutter_la_SOURCES) \
	$(cmap_extractcurve_la_SOURCES) \
        $(cmap_fdfit_la_SOURCES) \
        $(cmap_fztofd_la_SOURCES) \
	$(cmap_linestat_la_SOURCES) \
	$(cmap_lockin_la_SOURCES) \
        $(cmap_polylevel_la_SOURCES) \
        $(cmap_possearch_la_SOURCES) \
	$(cmap_simplemech_la_SOURCES) \
	$(cmap_sinebg_la_SOURCES) \
	cmap.c

cmap.c: $(srcdir)/Makefile.am $(top_srcdir)/utils/gen-module-bundle.sh
	$(SHELL) $(top_srcdir)/utils/gen-module-bundle.sh cmap.c $(bundlable_modules)

CLEAN_FILES = cmap.c

clean-local:
	rm -f core.* *~
