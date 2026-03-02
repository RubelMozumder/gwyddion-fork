dnl  Based on
dnl  ===========================================================================
dnl         http://www.gnu.org/software/autoconf-archive/ax_lib_hdf5.html
dnl  ===========================================================================
dnl  but modified heavily to get rid of the extra complexity caused by stuff we
dnl  do not want (parallel, Fortran).  And renamed.
dnl
dnl  SYNOPSIS
dnl
dnl    GWY_LIB_HDF5()
dnl
dnl  DESCRIPTION
dnl
dnl    This macro provides tests of the availability of serial HDF5 library.
dnl
dnl    The macro adds a --with-hdf5 option accepting one of three values:
dnl
dnl      no   - do not check for the HDF5 library.
dnl      yes  - do check for HDF5 library in standard locations.
dnl      path - complete path to the HDF5 helper script h5cc or h5pcc.
dnl
dnl    If HDF5 is successfully found, this macro calls
dnl
dnl      AC_SUBST(HDF5_VERSION)
dnl      AC_SUBST(HDF5_CC)
dnl      AC_SUBST(HDF5_CFLAGS)
dnl      AC_SUBST(HDF5_CPPFLAGS)
dnl      AC_SUBST(HDF5_LDFLAGS)
dnl      AC_SUBST(HDF5_LIBS)
dnl      AC_DEFINE(HAVE_HDF5)
dnl
dnl    and sets with_hdf5="yes".
dnl
dnl    If HDF5 is disabled or not found, this macros sets with_hdf5="no" and
dnl    with_hdf5_fortran="no".
dnl
dnl    Your configuration script can test $with_hdf to take any further
dnl    actions. HDF5_{C,CPP,LD}FLAGS may be used when building with C or C++.
dnl
dnl    To use the macro, one would code one of the following in "configure.ac"
dnl    before AC_OUTPUT:
dnl
dnl         GWY_LIB_HDF5()
dnl
dnl    One could test $with_hdf5 for the outcome or display it as follows
dnl
dnl      echo "HDF5 support:  $with_hdf5"
dnl
dnl  LICENSE
dnl
dnl    Copyright (c) 2009 Timothy Brown <tbrown@freeshell.org>
dnl    Copyright (c) 2010 Rhys Ulerich <rhys.ulerich@gmail.com>
dnl
dnl    Copying and distribution of this file, with or without modification, are
dnl    permitted in any medium without royalty provided the copyright notice
dnl    and this notice are preserved. This file is offered as-is, without any
dnl    warranty.

dnl serial 8

AC_DEFUN([GWY_LIB_HDF5], [

AC_REQUIRE([AC_PROG_SED])
AC_REQUIRE([AC_PROG_AWK])
AC_REQUIRE([AC_PROG_GREP])

dnl Add a default --with-hdf5 configuration option.
AC_ARG_WITH([hdf5],
  AS_HELP_STRING(
    [--with-hdf5=[yes/no/PATH]],[location of h5cc for HDF5 configuration]),
  [if test "$withval" = "no"; then
     with_hdf5="no"
   elif test "$withval" = "yes"; then
     with_hdf5="yes"
   else
     with_hdf5="yes"
     H5CC="$withval"
   fi],
   [with_hdf5="yes"]
)

dnl Set defaults to blank
HDF5_CC=""
HDF5_VERSION=""
HDF5_CFLAGS=""
HDF5_CPPFLAGS=""
HDF5_LDFLAGS=""
HDF5_LIBS=""

dnl Try and find hdf5 compiler tools and options.
if test "$with_hdf5" = "yes"; then
    if test -z "$H5CC"; then
        dnl Check to see if H5CC is in the path.
        dnl Do not look for a native one when cross-compiling.
        AC_PATH_TOOL([H5CC],[h5cc],[])
    else
        AC_MSG_CHECKING([Using provided HDF5 C wrapper])
        AC_MSG_RESULT([$H5CC])
    fi
    AC_MSG_CHECKING([for HDF5 libraries])
    if test ! -f "$H5CC" || test ! -x "$H5CC"; then
        AC_MSG_RESULT([no])
        AC_MSG_WARN([Unable to locate HDF5 compilation helper script 'h5cc'.
Please specify --with-hdf5=<LOCATION> as the full path to h5cc.
HDF5 support is being disabled (equivalent to --with-hdf5=no).])
        with_hdf5="no"
        with_hdf5_fortran="no"
    else
        dnl Get the h5cc output
        HDF5_SHOW=$(eval $H5CC -show)

        dnl Get the actual compiler used
        HDF5_CC=$(eval $H5CC -show | $AWK '{print $[]1}')

        dnl h5cc provides both AM_ and non-AM_ options
        dnl depending on how it was compiled either one of
        dnl these are empty. Lets roll them both into one.

        dnl Look for "HDF5 Version: X.Y.Z"
        HDF5_VERSION=$(eval $H5CC -showconfig | $GREP 'HDF5 Version:' | $AWK '{print $[]3}')

        AS_VERSION_COMPARE($HDF5_VERSION,$1,[HDF5_VERSION=])
        if test -n "$HDF5_VERSION"; then
            dnl A ideal situation would be where everything we needed was
            dnl in the AM_* variables. However most systems are not like this
            dnl and seem to have the values in the non-AM variables.
            dnl
            dnl We try the following to find the flags:
            dnl (1) Look for "NAME:" tags
            dnl (2) Look for "H5_NAME:" tags
            dnl (3) Look for "AM_NAME:" tags
            dnl
            HDF5_tmp_flags=$(eval $H5CC -showconfig \
                | $GREP 'FLAGS\|Extra libraries:' \
                | $AWK -F: '{printf("%s "), $[]2}' )

            dnl Find the installation directory and append include/
            HDF5_tmp_inst=$(eval $H5CC -showconfig \
                | $GREP 'Installation point:' \
                | $AWK -F: '{print $[]2}' )

            dnl Add this to the CPPFLAGS
            HDF5_CPPFLAGS="-I${HDF5_tmp_inst}/include"

            dnl Now sort the flags out based upon their prefixes
            for arg in $HDF5_SHOW $HDF5_tmp_flags ; do
            case "$arg" in
                -I*) echo $HDF5_CPPFLAGS | $GREP -e "$arg" 2>&1 >/dev/null \
                    || HDF5_CPPFLAGS="$arg $HDF5_CPPFLAGS"
                ;;
                -L*) echo $HDF5_LDFLAGS | $GREP -e "$arg" 2>&1 >/dev/null \
                    || HDF5_LDFLAGS="$arg $HDF5_LDFLAGS"
                ;;
                -l*) echo $HDF5_LIBS | $GREP -e "$arg" 2>&1 >/dev/null \
                    || HDF5_LIBS="$arg $HDF5_LIBS"
                ;;
            esac
            done

            HDF5_LIBS="$HDF5_LIBS -lhdf5"
            AC_MSG_RESULT([yes (version $[HDF5_VERSION])])

            dnl See if we can compile
            gwy_hdf5_save_CC=$CC
            gwy_hdf5_save_CPPFLAGS=$CPPFLAGS
            gwy_hdf5_save_LIBS=$LIBS
            gwy_hdf5_save_LDFLAGS=$LDFLAGS
            CC=$HDF5_CC
            CPPFLAGS=$HDF5_CPPFLAGS
            LIBS=$HDF5_LIBS
            LDFLAGS=$HDF5_LDFLAGS
            AC_CHECK_HEADER([hdf5.h], [ac_cv_hadf5_h=yes], [ac_cv_hadf5_h=no])
            dnl Check for H5free_memory which we actually require.
            AC_CHECK_LIB([hdf5], [H5free_memory], [ac_cv_libhdf5=yes], [ac_cv_libhdf5=no])
            if test "$ac_cv_hadf5_h" = "no" && test "$ac_cv_libhdf5" = "no" ; then
            AC_MSG_WARN([Unable to compile HDF5 test program])
            fi
            dnl Look for HDF5's high level library
            AC_CHECK_LIB([hdf5_hl],[main],[HDF5_LIBS="$HDF5_LIBS -lhdf5_hl"],[],[])
            ac_cv_lib_hdf5_hl=ac_cv_lib_hdf5_hl_main

            CC=$gwy_hdf5_save_CC
            CPPFLAGS=$gwy_hdf5_save_CPPFLAGS
            LIBS=$gwy_hdf5_save_LIBS
            LDFLAGS=$gwy_hdf5_save_LDFLAGS

            AC_SUBST([HDF5_VERSION])
            AC_SUBST([HDF5_CC])
            AC_SUBST([HDF5_CFLAGS])
            AC_SUBST([HDF5_CPPFLAGS])
            AC_SUBST([HDF5_LDFLAGS])
            AC_SUBST([HDF5_LIBS])
            AC_DEFINE([HAVE_HDF5], [1], [Defined if you have HDF5 support])
        else
            AC_MSG_RESULT([no, or too old])
            H5CC=
            HDF5_CC=
            with_hdf5=no
        fi
    fi
fi
])
