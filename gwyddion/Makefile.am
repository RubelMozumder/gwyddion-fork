# $Id: Makefile.am 21345 2018-08-27 18:48:04Z yeti-dn $

SUBDIRS = \
	m4 \
	libgwyddion \
	libprocess \
	libdraw \
	libgwydgets \
	libgwymodule \
	app \
	gwyddion \
	modules \
	plugins \
	po \
	pixmaps \
	perl \
	python \
	ruby \
	data \
	utils \
	thumbnailer \
	devel-docs

EXTRA_DIST = \
	autogen.sh \
	gwyddion.spec

DISTCLEANFILES = gwyconfig.h

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-gtk-doc \
	--enable-pygwy \
	--enable-maintainer-mode \
	--disable-desktop-file-update

ACLOCAL_AMFLAGS = -I m4

if MAINTAINER_MODE
dist-check-maintainer:
else
dist-check-maintainer:
	@echo "*** maintainer mode must be enabled in order to make dist"
	@false
endif

configexecincludedir = $(pkglibdir)/include
configexecinclude_DATA = gwyconfig.h

.PHONY: docs
