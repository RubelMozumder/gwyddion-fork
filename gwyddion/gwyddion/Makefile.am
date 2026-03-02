# $Id: Makefile.am 22445 2019-08-27 13:17:36Z yeti-dn $

libgwyddion = $(top_builddir)/libgwyddion/libgwyddion2.la
libgwymodule = $(top_builddir)/libgwymodule/libgwymodule2.la
libgwyapp = $(top_builddir)/app/libgwyapp2.la
libgwyprocess = $(top_builddir)/libprocess/libgwyprocess2.la
libgwydraw = $(top_builddir)/libdraw/libgwydraw2.la
libgwydgets = $(top_builddir)/libgwydgets/libgwydgets2.la

uidatadir = $(pkgdatadir)/ui

noinst_HEADERS = \
	mac_integration.h \
	authors.h \
	gwyddion.h \
	release.h \
	toolbox.h

uidata_DATA = toolbox.xml

bin_PROGRAMS = gwyddion

BUILT_SOURCES = \
	authors.h \
	release.h

EXTRA_DIST = toolbox.xml

if OS_DARWIN
darwin_cflags = -ObjC -g
darwin_ldflags = -framework AppKit -framework ApplicationServices
endif

AM_CFLAGS = $(darwin_cflags) @GTK_MAC_CFLAGS@ @FFTW3_CFLAGS@ @UNIQUE_CFLAGS@ @COMMON_CFLAGS@ @GIO_CFLAGS@ @OPENMP_CFLAGS@
AM_CPPFLAGS = -DG_LOG_DOMAIN=\"Gwyddion-Program\" -I$(top_srcdir) @OPENMP_CFLAGS@
AM_LDFLAGS = $(darwin_ldflags) @OPENMP_CFLAGS@

gwyddion_SOURCES = \
	about.c \
	gwyddion.c \
	mac_integration.c \
	remote.c \
	remote-unique.c \
	remote-win32.c \
	remote-x11.c \
	splash.c \
	tips.c \
	toolbox.c \
	toolbox-editor.c \
	toolbox-spec.c

gwyddion_LDADD = @COMMON_LDFLAGS@ @GTK_MAC_LIBS@ @GTKGLEXT_LIBS@ @FFTW3_LIBS@ @UNIQUE_LIBS@ @GIO_LIBS@ @BASIC_LIBS@ $(LIBXMU) \
	$(libgwyapp) \
	$(libgwymodule) \
	$(libgwydgets) \
	$(libgwydraw) \
	$(libgwyprocess) \
	$(libgwyddion)

if MAINTAINER_MODE
authors.h: ${top_srcdir}/AUTHORS $(top_srcdir)/utils/gen-authors.py
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/utils/gen-authors.py $(top_srcdir)/AUTHORS header >authors.h

release.h: ${top_srcdir}/NEWS Makefile $(top_builddir)/config.h
	$(AM_V_GEN)echo '/* This is a 'GENERATED' file. */' >release.h
	$(AM_V_at)sed -n 1p ${top_srcdir}/NEWS | sed -e 's/^.*(/#define RELEASEDATE /' -e 's/)/u/' -e 's/-//g' >>release.h
endif

clean-local:
	rm -f core.* *~
