# $Id: Makefile.am 26351 2024-05-20 14:11:17Z yeti-dn $

libgwyddion = $(top_builddir)/libgwyddion/libgwyddion2.la
libgwyprocess = $(top_builddir)/libprocess/libgwyprocess2.la

libgwydraw2includedir = $(pkgincludedir)/libdraw

if MODULE_DEPENDENCIES
libgwydraw_libadd = \
	$(libgwyprocess) \
	$(libgwyddion)
endif

libgwydraw2include_HEADERS = \
	gwydraw.h \
	gwydrawenums.h \
	gwydrawtypes.h \
	gwyglmaterial.h \
	gwygradient.h \
	gwypixfield.h \
	gwyrgba.h \
	gwyselection.h

lib_LTLIBRARIES = libgwydraw2.la

if OS_MSWIN
no_undefined = -no-undefined
export_symbols = -export-symbols libgwydraw2.def
libgwydraw2_la_DEPENDENCIES = libgwydraw2.def
endif

# Must init
EXTRA_DIST =
CLEANFILES = libgwydraw2.def
MAINTAINERCLEANFILES =
BUILT_SOURCES =

MKENUM_NAME = gwydrawtypes
MKENUM_ID = GWYDRAW_TYPES
MKENUM_HFILES = $(srcdir)/gwydrawenums.h
include $(top_srcdir)/utils/mkenum.mk

noinst_HEADERS = \
	gwydrawinternal.h

libgwydraw2.def: $(libgwydraw2_la_OBJECTS)
	$(AM_V_GEN)echo EXPORTS >libgwydraw2.def
	$(AM_V_at)for lo in $(libgwydraw2_la_OBJECTS); do \
		o=`sed "s/^pic_object='\\(.*\\)'/\\1/;t;d" "$$lo"`; \
		$(NM) --defined-only --demangle --no-sort "$$o" \
		| awk -F' ' '($$2=="T" && $$3 ~ /^gwy_/){print "     ",$$3}'; \
	done | sort >>libgwydraw2.def

AM_CFLAGS = @COMMON_CFLAGS@ @OPENMP_CFLAGS@
AM_CPPFLAGS = -I$(top_srcdir) -DG_LOG_DOMAIN=\"GwyDraw\" @OPENMP_CFLAGS@

# In following order do:
# Any code change     C:   R++: A
# Any iface change    C++: 0:   A
# Adding ifaces       C:   R:   A++
# Changing ifaces     C:   R:   0
libversion = -version-info 7:4:7
#libversion = -release @LIBRARY_RELEASE@
libgwydraw2_la_LDFLAGS = @GTK_LIBS@ @BASIC_LIBS@ @OPENMP_CFLAGS@ -export-dynamic $(no_undefined) $(export_symbols) $(libversion)
libgwydraw2_la_LIBADD = $(libgwydraw_libadd)

libgwydraw2_la_SOURCES = \
	gwydraw.c \
	gwydrawtypes.c \
	gwyglmaterial.c \
	gwygradient.c \
	gwypixfield.c \
	gwyrgba.c \
	gwyselection.c

clean-local:
	rm -f core.* *~

