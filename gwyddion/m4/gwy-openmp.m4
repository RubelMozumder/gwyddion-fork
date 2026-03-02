## Check OpenMP version.  Either you support at least some minimal version $1
## or we ignore you.  This allows us to avoid littering code with buggy
## conditional nonsense.  This file is in public domain.
##
AC_DEFUN([GWY_OPENMP],
[AC_REQUIRE([AC_OPENMP])dnl
dnl

dnl Check whether C compiler supports our minimum OpenMP version.  Try to
dnl disable OpenMP when it does not.
if test "x$ac_cv_prog_c_openmp" = xunsupported; then
  enable_openmp=no
else
AC_CACHE_CHECK([whether $CC supports OpenMP $2],
  [ac_cv_prog_cc_openmp_$1],
  [ye_PROG_CC_OPTION_cflags="$CFLAGS"
  CFLAGS="$CFLAGS $OPENMP_CFLAGS"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#if (_OPENMP >= $1)
#else
choke me
#endif]])],
     [ac_cv_prog_cc_openmp_$1=yes],
     [ac_cv_prog_cc_openmp_$1=no])
  CFLAGS="$ye_PROG_CC_OPTION_cflags"
])
enable_openmp=$ac_cv_prog_cc_openmp_$1
fi
if test x$enable_openmp != xyes; then
  OPENMP_CFLAGS=
  OPENMP_CXXFLAGS=
fi])

