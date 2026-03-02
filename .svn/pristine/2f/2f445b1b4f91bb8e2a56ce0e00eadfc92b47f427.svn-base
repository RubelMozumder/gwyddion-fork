AC_INIT([Gwyddion user guide], [2009-11-11], [klapetek@gwyddion.net])
AC_CONFIG_SRCDIR(common/xhtml.xsl)
AC_CONFIG_FILES(\
  Makefile \
  common/Makefile \
  common/inkscape-export.sh \
  common/render.sh \
  common/xmlpo.sh \
  common/user-guide.iss \
  en/Makefile \
  fr/Makefile \
  ru/Makefile \
)

LANGUAGES="en fr ru"
AC_SUBST(LANGUAGES)

PACKAGE_SHORTNAME=user-guide
AC_SUBST(PACKAGE_SHORTNAME)

GWY_HTML_MISSING=
GWY_PDF_MISSING=
GWY_PNG_MISSING=
GWY_SVG_MISSING=
GWY_MAN_MISSING=

GWY_NO=no

AC_PROG_CC

AC_CHECK_PROGS([GMAKE],[gmake make],[no])
if test "$GMAKE" != no; then
  AC_MSG_CHECKING([whether $GMAKE is GNU make])
  if $GMAKE --version 2>/dev/null | grep '^GNU Make' >/dev/null; then
    AC_MSG_RESULT([seems so])
  else
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([GNU make is required to build user guide
                  Get it at http://www.gnu.org/software/make/])
  fi
fi

AC_CHECK_PROGS([GSED],[gsed sed],[no])
if test "$GSED" != no; then
  AC_MSG_CHECKING([whether $GSED is GNU sed])
  if $GSED --version 2>/dev/null | grep '^GNU sed' >/dev/null; then
    AC_MSG_RESULT([seems so])
  else
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([GNU sed is required to build the user guide.
                  Get it at http://ftp.gnu.org/pub/gnu/sed/])
  fi
fi

AC_CHECK_PROGS([BASH],[bash],[no])
if test "$GSED" == no; then
  AC_MSG_ERROR([Bash is required to build the user guide.
                Get it at http://ftp.gnu.org/pub/gnu/bash/])
fi

# FIXME: Text processing probably does not actually work with Python3.
AC_CHECK_PROGS([PYTHON],[python2.7 python2.6 python2.5 python2.4 python2],[no])
if test "$PYTHON" == no; then
  AC_MSG_ERROR([Python 2.7 is required to build the user guide.
                Get it at http://www.python.org/])
fi

JH_CHECK_XML_CATALOG([-//OASIS//DTD DocBook XML V4.5//EN],
                     [DocBook XML DTD V4.5])

AC_CHECK_PROGS([XSLTPROC],[xsltproc],[no])
GWY_MISSING(XSLTPROC,[HTML PDF MAN],[xsltproc, get libxml2 from http://xmlsoft.org/])

AC_CHECK_PROGS([PDFLATEX],[pdflatex],[no])
GWY_MISSING(PDFLATEX,[HTML PDF],[tex, get pdfLaTeX from http://tug.org/])

AC_CHECK_PROGS([PDF2SVG],[pdf2svg],[no])
GWY_MISSING(PDF2SVG,[HTML],[pdf2svg, get pdf2svg from http://cityinthesky.co.uk/opensource/pdf2svg/])

AC_CHECK_PROGS([DBLATEX],[dblatex],[no])
GWY_MISSING(DBLATEX,[PDF],[dblatex, get DBLaTeX from http://dblatex.sourceforge.net/])

AC_CHECK_PROGS([INKSCAPE],[inkscape],[no])
GWY_MISSING(INKSCAPE,[HTML PDF SVG],[inkscape, get Inkscape from http://www.inkscape.org/])
INKSCAPE_EXPORT_NEWSTYLE=no
if test "$INKSCAPE" != no; then
  AC_MSG_CHECKING([whether inkscape has 1.0-style command line options])
  inkscape_version=$($INKSCAPE --version 2>&1 | grep '^Inkscape' | sed -e 's/^Inkscape  *//' -e 's/ .*$//')
  GWY_VERSION_IFELSE($inkscape_version,[1.0.0],[INKSCAPE_EXPORT_NEWSTYLE=yes])
  AC_MSG_RESULT($INKSCAPE_EXPORT_NEWSTYLE)
fi
AC_SUBST([INKSCAPE_EXPORT_NEWSTYLE])

AC_CHECK_PROGS([XML2PO],[xml2po],[])
AC_CHECK_PROGS([XML2POT],[xml2pot],[])
AC_CHECK_PROGS([PO2XML],[po2xml],[])
if test "x$XML2PO" = x; then
    if test "x$XML2POT" = x -o "x$PO2XML" = x; then
GWY_MISSING(XML2PO,[HTML PDF SVG],[xml2po, get gnome-doc-utils from http://www.gnome.org/])
    fi
fi

AC_CHECK_PROGS([MSGMERGE],[msgmerge],[no])
GWY_MISSING(MSGMERGE,[HTML PDF SVG],[msgmerge, get gettext from http://www.gnu.org/software/gettext/])

AC_CHECK_PROGS([MSGCAT],[msgcat],[no])
GWY_MISSING(MSGCAT,[HTML PDF SVG],[msgcat, get gettext from http://www.gnu.org/software/gettext/])

AC_CHECK_PROGS([MSGGREP],[msggrep],[no])
GWY_MISSING(MSGGREP,[HTML PDF SVG],[msggrep, get gettext from http://www.gnu.org/software/gettext/])

AC_CHECK_PROGS([HIGHLIGHT_PROGRAM],[source-highlight],[:])

AC_MSG_CHECKING([for TeX cyrillic T2 encoding support])
if kpsewhich mathtext.sty >/dev/null; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
  GWY_MISSING(GWY_NO,[HTML PDF],[mathtext.sty, get LaTeX t2 package from CTAN])
fi

AC_MSG_CHECKING([for TeX cyrillic T2A encoding support])
if kpsewhich t2aenc.def >/dev/null; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
  GWY_MISSING(GWY_NO,[HTML PDF],[t2aenc.def, get LaTeX cyrillic package from CTAN])
fi

AC_MSG_CHECKING([for TeX package lh cyrillic])
if kpsewhich lh-lcy.sty >/dev/null; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
  GWY_MISSING(GWY_NO,[PDF],[lh-lcy.sty, get LaTeX lh package from CTAN])
fi

AC_MSG_CHECKING([for TeX Russian support])
if kpsewhich russianb.ldf >/dev/null; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
  GWY_MISSING(GWY_NO,[PDF],[russianb.ldf, get LaTeX babel russian package from CTAN])
fi

AC_MSG_CHECKING([for TeX French support])
if kpsewhich french.ldf >/dev/null; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
  GWY_MISSING(GWY_NO,[PDF],[french.ldf, get LaTeX babel french package from CTAN])
fi

AC_MSG_CHECKING([for TeX package multirow.sty])
if kpsewhich multirow.sty >/dev/null; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
  GWY_MISSING(GWY_NO,[PDF],[multirow.sty, get LaTeX multirow package from CTAN])
fi

# Here we assume if someone has xhtml he has manpages too.
JH_CHECK_XML_CATALOG([http://docbook.sourceforge.net/release/xsl/current/xhtml/chunk.xsl],
                     [DocBook XSL Stylesheets],
                     [],[GWY_MISSING(GWY_NO,[HTML],[DocBook XSL Stylesheets, get them from http://docbook.sourceforge.net/projects/xsl/])])

if test -z "$GWY_HTML_MISSING"; then
  AC_MSG_CHECKING([whether DocBook XSL Stylesheets version is >= 1.70])
  cat >conftest.xml <<_ACEOF
<?xml version='1.0'?>
<!DOCTYPE book PUBLIC '-//OASIS//DTD DocBook XML V4.5//EN' 'http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd'>
<book/>
_ACEOF
  if GWY_RUN_LOG($XSLTPROC --nowrite docbook-xsl-version.xsl conftest.xml); then
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
    GWY_MISSING(GWY_NO,[HTML],[DocBook XSL Stylesheets are too old, get them from http://docbook.sourceforge.net/projects/xsl/])
  fi
  rm -f conftest.xml
fi

PKG_CHECK_MODULES([PNG],[libpng],,[GWY_MISSING(GWY_NO,[PNG],[libpng, get libpng from http://www.libpng.org/pub/png/])])

if test x = "x$GWY_HTML_MISSING"; then XHTML_ENABLE=xhtml; else XHTML_ENABLE=; fi
if test x = "x$GWY_PDF_MISSING"; then PDF_ENABLE=pdf; else PDF_ENABLE=; fi
if test x = "x$GWY_MAN_MISSING"; then MAN_ENABLE=man; else MAN_ENABLE=; fi
if test x = "x$GWY_PNG_MISSING"; then PNG_ENABLE=png; else PNG_ENABLE=; fi
if test x = "x$GWY_SVG_MISSING"; then SVG_ENABLE=svg; else SVG_ENABLE=; fi

AC_SUBST([XHTML_ENABLE])
AC_SUBST([PDF_ENABLE])
AC_SUBST([MAN_ENABLE])
AC_SUBST([PNG_ENABLE])
AC_SUBST([SVG_ENABLE])

AC_OUTPUT

if test x != "x$GWY_HTML_MISSING"; then
AC_MSG_WARN([Cannot build HTML, the following tools were not found:
$GWY_HTML_MISSING])
fi

if test x != "x$GWY_PDF_MISSING"; then
AC_MSG_WARN([Cannot build PDF, the following tools were not found:
$GWY_PDF_MISSING])
fi

if test x != "x$GWY_MAN_MISSING"; then
AC_MSG_WARN([Cannot build man pages, the following tools were not found:
$GWY_MAN_MISSING])
fi

if test x != "x$GWY_PNG_MISSING"; then
AC_MSG_WARN([Cannot build fixpngres, the following tools were not found:
$GWY_PNG_MISSING])
fi

echo "Run '$GMAKE help' for some help."
