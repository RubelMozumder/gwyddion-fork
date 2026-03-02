## Check for options compiler supports.  This file is in public domain.
##
AC_DEFUN([GWY_PROG_CC_OPTION],
[AC_REQUIRE([AC_PROG_CC])dnl
dnl

dnl Check whether C compiler accepts option $2, set PROG_CC_$1 to either
dnl the option or empty.  On success, $3 is executed, on failure $4.
dnl
dnl We must pass -Wno-blah as -Wblah because gcc does not fail when we attempt
dnl to switch off unknown warnings.
dnl
dnl We must also add -Werror because clang otherwise never returns non-zero
dnl status anyway.
AC_CACHE_CHECK([whether $CC knows $2],
  [ac_cv_prog_cc_option_$1],
  [ye_PROG_CC_OPTION_cflags="$CFLAGS"
   CFLAGS="$CFLAGS -Werror `echo x$2 | sed 's/^x-Wno-/x-W/' | sed 's/^x//'`"
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM()],
     [ac_cv_prog_cc_option_$1=yes],
     [ac_cv_prog_cc_option_$1=no])
   CFLAGS="$ye_PROG_CC_OPTION_cflags"])
if test "$ac_cv_prog_cc_option_$1" = "yes"; then
  PROG_CC_$1="$2"
  $3
else
  PROG_CC_$1=
  $4
fi
])


