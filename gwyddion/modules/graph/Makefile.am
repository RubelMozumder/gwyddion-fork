# $Id: Makefile.am 25620 2023-09-05 13:28:56Z yeti-dn $

graphdir = $(pkglibdir)/modules/graph

# Only bundle modules without own dependencies.  If something goes wrong with
# any required libraries at run-time, it will at most break these individual
# unbundled modules, not the bundle.
bundlable_modules = \
	graph_align.la \
	graph_cd.la \
	graph_cut.la \
	graph_dos_spectrum.la \
	graph_export_ascii.la \
	graph_export_bitmap.la \
	graph_export_vector.la \
	graph_fdfit.la \
	graph_filter.la \
	graph_fit.la \
	graph_flip.la \
	graph_fztofd.la \
	graph_invert.la \
	graph_level.la \
	graph_logscale.la \
	graph_measure_period.la \
	graph_merge.la \
	graph_peaks.la \
	graph_polylevel.la \
	graph_sfuncs.la \
	graph_simplemech.la \
	graph_sinebg.la \
	graph_stats.la \
	graph_terraces.la

# graph_modules is either the bundle or all the individual modules.
if MODULE_BUNDLING
graph_modules = graph.la
BUILT_SOURCES = graph.c
graph_la_CFLAGS = $(AM_CFLAGS) -DGWY_MODULE_BUNDLING
else
graph_modules = $(bundlable_modules)
endif

graph_LTLIBRARIES = \
	$(graph_modules)

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
AM_LDFLAGS = -avoid-version -module $(no_undefined) $(module_libadd)

# Bundlable modules, must be listed in tools_la_SOURCES below.
graph_align_la_SOURCES          = graph_align.c
graph_cd_la_SOURCES             = graph_cd.c
graph_cut_la_SOURCES            = graph_cut.c
graph_dos_spectrum_la_SOURCES   = graph_dos_spectrum.c
graph_export_ascii_la_SOURCES   = graph_export_ascii.c
graph_export_bitmap_la_SOURCES  = graph_export_bitmap.c
graph_export_vector_la_SOURCES  = graph_export_vector.c
graph_fdfit_la_SOURCES          = graph_fdfit.c
graph_filter_la_SOURCES         = graph_filter.c
graph_fit_la_SOURCES            = graph_fit.c
graph_flip_la_SOURCES           = graph_flip.c
graph_fztofd_la_SOURCES         = graph_fztofd.c
graph_invert_la_SOURCES         = graph_invert.c
graph_level_la_SOURCES          = graph_level.c
graph_logscale_la_SOURCES       = graph_logscale.c
graph_measure_period_la_SOURCES = graph_measure_period.c
graph_merge_la_SOURCES          = graph_merge.c
graph_peaks_la_SOURCES          = graph_peaks.c
graph_polylevel_la_SOURCES      = graph_polylevel.c
graph_sfuncs_la_SOURCES         = graph_sfuncs.c
graph_simplemech_la_SOURCES     = graph_simplemech.c
graph_sinebg_la_SOURCES         = graph_sinebg.c
graph_stats_la_SOURCES          = graph_stats.c
graph_terraces_la_SOURCES       = graph_terraces.c

graph_la_SOURCES = \
	$(graph_align_la_SOURCES) \
	$(graph_cd_la_SOURCES) \
	$(graph_cut_la_SOURCES) \
	$(graph_dos_spectrum_la_SOURCES) \
	$(graph_export_ascii_la_SOURCES) \
	$(graph_export_bitmap_la_SOURCES) \
	$(graph_export_vector_la_SOURCES) \
	$(graph_fdfit_la_SOURCES) \
	$(graph_filter_la_SOURCES) \
	$(graph_fit_la_SOURCES) \
	$(graph_flip_la_SOURCES) \
	$(graph_fztofd_la_SOURCES) \
	$(graph_invert_la_SOURCES) \
	$(graph_level_la_SOURCES) \
	$(graph_logscale_la_SOURCES) \
	$(graph_measure_period_la_SOURCES) \
	$(graph_merge_la_SOURCES) \
	$(graph_peaks_la_SOURCES) \
	$(graph_polylevel_la_SOURCES) \
	$(graph_sfuncs_la_SOURCES) \
	$(graph_simplemech_la_SOURCES) \
	$(graph_sinebg_la_SOURCES) \
	$(graph_stats_la_SOURCES) \
	$(graph_terraces_la_SOURCES) \
	graph.c

graph.c: $(srcdir)/Makefile.am $(top_srcdir)/utils/gen-module-bundle.sh
	$(SHELL) $(top_srcdir)/utils/gen-module-bundle.sh graph.c $(bundlable_modules)

CLEAN_FILES = graph.c

clean-local:
	rm -f core.* *~
