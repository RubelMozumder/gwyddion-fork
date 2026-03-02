# $Id: Makefile.am 26225 2024-02-29 16:49:48Z yeti-dn $

# For each optional module foo there is variable foo_module that should be set
# to foo.la when the module is enabled.
#
# If a module is disabled, add its source files to disabled_sources.  This
# ensures they are still included in tarballs, preserves translations, etc.
disabled_sources =

hdf5_sources = \
	hdf5file.c \
	datxfile.c \
	epflfile.c \
	ergofile.c \
	nsidfile.c \
	shilpsfile.c \
	gwyhdf5.c

# We never bundle since we only have one module here (we put all the hdf5
# functions to one plain module, not a bundle). It requires HDF5 to compile.
# When you add a new file type you must also add a prototype of its
# registration function to hdf5file.h as G_GNUC_INTERNAL and call it in
# hdf5file.c module_register().
if HAVE_HDF5
file_modules        = hdf5file.la
hdf5file_la_SOURCES = $(hdf5_sources)
hdf5file_la_CFLAGS  = $(AM_CFLAGS) @HDF5_CPPFLAGS@ @HDF5_CFLAGS@
hdf5file_la_LIBADD  = @HDF5_LDFLAGS@ @HDF5_LIBS@
else
file_modules        =
disabled_sources   += $(hdf5_sources)
endif

filedir = $(pkglibdir)/modules/file

# Modules listed indirectly via a variable are built optionally.
file_LTLIBRARIES = $(file_modules)

noinst_HEADERS = \
	gwyhdf5.h \
	hdf5file.h

EXTRA_DIST = $(disabled_sources)

if OS_MSWIN
no_undefined = -no-undefined
endif

AM_CPPFLAGS = -I$(top_srcdir) -I$(srcdir)/.. -DG_LOG_DOMAIN=\"Module\"
AM_CFLAGS = @COMMON_CFLAGS@
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

clean-local:
	rm -f core.* *~
