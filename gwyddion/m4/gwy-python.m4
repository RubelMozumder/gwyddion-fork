dnl Try to link a test program with already determined PYTHON_INCLUDES and
dnl PYTHON_LDFLAGS (by whatever means necessary).
dnl $1 = action on success
dnl $2 = action on failure

AC_DEFUN([GWY_PYTHON_TRY_LINK],
[ac_save_CPPFLAGS="$CPPFLAGS"
ac_save_LIBS="$LIBS"
CPPFLAGS="$LIBS $PYTHON_INCLUDES"
LIBS="$LIBS $PYTHON_LDFLAGS"
AC_MSG_CHECKING([if we can link a test Python program])
AC_LANG_PUSH([C])
AC_LINK_IFELSE([
AC_LANG_PROGRAM([[#include <Python.h>]],
                [[Py_Initialize();]])
],dnl
[AC_MSG_RESULT([yes])
$1],dnl
[AC_MSG_RESULT([no])
$2])
AC_LANG_POP([C])
CPPFLAGS="$ac_save_CPPFLAGS"
LIBS="$ac_save_LIBS"
])

dnl Find flags for with-Python compilation and linking.
dnl   Originally suggested by wichert.
dnl   Rewritten by Yeti.  Also added the pkg-config method.
dnl $1 = list of Python versions to try
dnl $2 = action on success
dnl $3 = action on failure
AC_DEFUN([GWY_PYTHON_DEVEL],
[have_libpython_pkg=no
if test "x$1" != x; then
  for version in $1 ; do
    AC_PATH_TOOL([PYTHON_CONFIG], [python$version-config])
    if test "x$PYTHON_CONFIG" != x; then
      PYTHON_VERSION=$version
      break
    fi
    PKG_CHECK_MODULES([PYTHON],[python-$version],[have_libpython_pkg=yes],[:])
    if test $have_libpython_pkg != no; then
      PYTHON_VERSION=$version
      break
    fi
  done
else
  AC_PATH_TOOL([PYTHON_CONFIG], [python-config])
  if test "x$PYTHON_CONFIG" != x; then
    PYTHON_VERSION=$(python -c "import sys;print '.'.join(map(str, sys.version_info@<:@:2@:>@))")
  fi
fi

if test "x$PYTHON_CONFIG" != x; then
  if test "x$PYTHON_INCLUDES" = x; then
    PYTHON_INCLUDES="$("$PYTHON_CONFIG" --includes)"
  fi
  if test "x$PYTHON_LDFLAGS" = x; then
    PYTHON_LDFLAGS="$("$PYTHON_CONFIG" --ldflags)"
  fi
  GWY_PYTHON_TRY_LINK([],[PYTHON_CONFIG=])
fi

if test $have_libpython_pkg != no; then
  if test "x$PYTHON_INCLUDES" = x; then
    PYTHON_INCLUDES="$PYTHON_CFLAGS"
  fi
  if test "x$PYTHON_LDFLAGS" = x; then
    PYTHON_LDFLAGS="$PYTHON_LIBS"
  fi
  GWY_PYTHON_TRY_LINK([],[have_libpython_pkg=no])
fi

if test $have_libpython_pkg != no -o "x$PYTHON_CONFIG" != x; then
  $2
  :
else
  PYTHON_INCLUDES=
  PYTHON_LDFLAGS=
  $3
fi

AC_SUBST([PYTHON_VERSION])
AC_SUBST([PYTHON_INCLUDES])
AC_SUBST([PYTHON_LDFLAGS])
])

