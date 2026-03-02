# $Id: Makefile.am 26351 2024-05-20 14:11:17Z yeti-dn $

libgwyddion = $(top_builddir)/libgwyddion/libgwyddion2.la
libgwymodule = $(top_builddir)/libgwymodule/libgwymodule2.la
libgwyprocess = $(top_builddir)/libprocess/libgwyprocess2.la
libgwydraw = $(top_builddir)/libdraw/libgwydraw2.la
libgwydgets = $(top_builddir)/libgwydgets/libgwydgets2.la

libgwyapp2includedir = $(pkgincludedir)/app

if MODULE_DEPENDENCIES
libgwyapp_libadd = \
	$(libgwymodule) \
	$(libgwydgets) \
	$(libgwydraw) \
	$(libgwyprocess) \
	$(libgwyddion)
endif

libgwyapp2include_HEADERS = \
	app.h \
	data-browser.h \
	datachooser.h \
	dialog.h \
	file.h \
	filelist.h \
	funcuse.h \
	glmaterial-editor.h \
	gradient-editor.h \
	gwyapp.h \
	gwyapptypes.h \
	gwymoduleutils.h \
	gwymoduleutils-file.h \
	gwymoduleutils-synth.h \
	gwyplaintool.h \
	gwytool.h \
	help.h \
	log.h \
	logging.h \
	menu.h \
	meta.h \
	param-def.h \
	params.h \
	param-resource.h \
	param-table.h \
	resource-editor.h \
	gwyresultsexport.h \
	settings.h \
	undo.h \
	validate.h \
	wait.h

noinst_HEADERS = \
	gwyappfilechooser.h \
	gwyappinternal.h \
	param-internal.h

lib_LTLIBRARIES = libgwyapp2.la

if OS_MSWIN
no_undefined = -no-undefined
export_symbols = -export-symbols libgwyapp2.def
win32_libs = -lole32
libgwyapp2_la_DEPENDENCIES = libgwyapp2.def
endif

# Must init
EXTRA_DIST =
CLEANFILES = libgwyapp2.def
MAINTAINERCLEANFILES =
BUILT_SOURCES =

MKENUM_NAME = gwyapptypes
MKENUM_ID = GWY_APP_TYPES
MKENUM_HFILES = \
	$(srcdir)/data-browser.h \
	$(srcdir)/dialog.h \
	$(srcdir)/gwymoduleutils.h \
	$(srcdir)/gwymoduleutils-file.h \
	$(srcdir)/gwymoduleutils-synth.h \
	$(srcdir)/gwyplaintool.h \
	$(srcdir)/gwyresultsexport.h \
	$(srcdir)/gwytool.h \
	$(srcdir)/help.h \
	$(srcdir)/logging.h \
	$(srcdir)/menu.h \
	$(srcdir)/settings.h \
	$(srcdir)/validate.h
include $(top_srcdir)/utils/mkenum.mk

if OS_DARWIN
darwin_cflags = -ObjC -g
darwin_ldflags = -framework AppKit -framework ApplicationServices
endif

libgwyapp2.def: $(libgwyapp2_la_OBJECTS)
	$(AM_V_GEN)echo EXPORTS >libgwyapp2.def
	$(AM_V_at)for lo in $(libgwyapp2_la_OBJECTS); do \
		o=`sed "s/^pic_object='\\(.*\\)'/\\1/;t;d" "$$lo"`; \
		$(NM) --defined-only --demangle --no-sort "$$o" \
		| awk -F' ' '($$2=="T" && $$3 ~ /^gwy_/){print "     ",$$3}'; \
	done | sort >>libgwyapp2.def

# In following order do:
# Any code change     C:   R++: A
# Any iface change    C++: 0:   A
# Adding ifaces       C:   R:   A++
# Changing ifaces     C:   R:   0
libversion = -version-info 30:2:30
#libversion = -release @LIBRARY_RELEASE@
AM_CFLAGS = @COMMON_CFLAGS@ @GIO_CFLAGS@ @OPENMP_CFLAGS@
AM_LDFLAGS = @GTKGLEXT_LIBS@ @GIO_LIBS@ @BASIC_LIBS@ $(win32_libs) -export-dynamic $(no_undefined) $(export_symbols) $(libversion) $(libgwyapp_libadd)
AM_CPPFLAGS = -I$(top_srcdir) -DG_LOG_DOMAIN=\"GwyApp\" @OPENMP_CFLAGS@

libgwyapp2_la_SOURCES = \
	app.c \
	data-browser.c \
	data-browser-aux.c \
	datachooser.c \
	dialog.c \
	file.c \
	filelist.c \
	funcuse.c \
	glmaterial-editor.c \
	gradient-editor.c \
	gwyappfilechooser.c \
	gwyapptypes.c \
	gwymoduleutils.c \
	gwymoduleutils-file.c \
	gwymoduleutils-synth.c \
	gwyplaintool.c \
	gwytool.c \
	help.c \
	log.c \
	logging.c \
	menu.c \
	meta.c \
	param-def.c \
	params.c \
	param-resource.c \
	param-table.c \
	settings.c \
	resource-editor.c \
	gwyresultsexport.c \
	undo.c \
	validate.c \
	wait.c

clean-local:
	rm -f core.* *~
