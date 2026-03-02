%{?mingw_package_header}

Name:           mingw-hdf5
Version:        1.10.8
Release:        2%{?dist}
Summary:        MinGW Windows HDF5 library
License:        BSD

Group:          Development/Libraries
URL:            https://portal.hdfgroup.org/display/HDF5/HDF5
Source0:        https://support.hdfgroup.org/ftp/HDF5/current18/src/hdf5-%{version}.tar.bz2

Patch0:  hdf5-LD_LIBRARY_PATH.patch
# Properly run MPI_Finalize() in t_pflush1 (don't need)
#Patch1: hdf5-mpi.patch
# Fix long double conversions on ppc64le (don't need)
# https://bugzilla.redhat.com/show_bug.cgi?id=1078173
#Patch3: hdf5-ldouble-ppc64le.patch

Patch9:  hdf5-win32.patch

BuildArch:      noarch

BuildRequires:  mingw32-filesystem >= 133
BuildRequires:  mingw64-filesystem >= 133
BuildRequires:  mingw32-gcc
BuildRequires:  mingw64-gcc
BuildRequires:  mingw32-gcc-c++
BuildRequires:  mingw64-gcc-c++
BuildRequires:  mingw32-binutils
BuildRequires:  mingw64-binutils
BuildRequires:  mingw32-dlfcn
BuildRequires:  mingw64-dlfcn
BuildRequires:  mingw32-zlib
BuildRequires:  mingw64-zlib
BuildRequires:  mingw32-libaec
BuildRequires:  mingw64-libaec
# For H5detect and possibly more things.
BuildRequires:  wine-core(x86-64)
BuildRequires:  wine-core(x86-32)
# For libtool path conversion (winepath)
BuildRequires:  wine-common

# For patches/rpath
BuildRequires: automake
BuildRequires: libtool
BuildRequires: make


%description
HDF5 is a general purpose library and file format for storing scientific data.
HDF5 can store two primary objects: datasets and groups. A dataset is
essentially a multidimensional array of data elements, and a group is a
structure for organizing objects in an HDF5 file. Using these two basic
objects, one can create and store almost any kind of scientific data
structure, such as images, arrays of vectors, and structured and unstructured
grids. You can also mix and match them in HDF5 files according to your needs.

This package contains the MinGW Windows cross-compiled HDF5 library.

%package -n     mingw32-hdf5
Summary:        MinGW Windows HDF5 library

%description -n mingw32-hdf5
HDF5 is a general purpose library and file format for storing scientific data.
HDF5 can store two primary objects: datasets and groups. A dataset is
essentially a multidimensional array of data elements, and a group is a
structure for organizing objects in an HDF5 file. Using these two basic
objects, one can create and store almost any kind of scientific data
structure, such as images, arrays of vectors, and structured and unstructured
grids. You can also mix and match them in HDF5 files according to your needs.

This package contains the MinGW Windows cross-compiled HDF5 library.

%package -n     mingw64-hdf5
Summary:        MinGW Windows HDF5 library

%description -n mingw64-hdf5
HDF5 is a general purpose library and file format for storing scientific data.
HDF5 can store two primary objects: datasets and groups. A dataset is
essentially a multidimensional array of data elements, and a group is a
structure for organizing objects in an HDF5 file. Using these two basic
objects, one can create and store almost any kind of scientific data
structure, such as images, arrays of vectors, and structured and unstructured
grids. You can also mix and match them in HDF5 files according to your needs.

This package contains the MinGW Windows cross-compiled HDF5 library.


%{?mingw_debug_package}


%prep
%setup -q -n hdf5-%{version}
%patch0 -p1 -b .LD_LIBRARY_PATH
%patch9 -p1 -b .win32

# Force -no-undefined.  The HDF5 build system kind of tries to use this flag,
# but it is still missing in a bunch of places.
grep -l -r --include 'Makefile.*' 'lib.*la.*LDFLAGS' . \
    | xargs sed -i 's|lib.*la.*LDFLAGS *=|\0 -no-undefined |'

# Force shared by default for compiler wrappers (bug #1266645)
sed -i -e '/^STATIC_AVAILABLE=/s/=.*/=no/' */*/h5[cf]*.in
autoreconf -f -i

# Modify low optimization level for gnu compilers
sed -e 's|-O -finline-functions|-O3 -finline-functions|g' -i config/gnu-flags

# Fix guessing things wrong according to uname.  And yes, it wants it
# uppercase.
sed -i 's|"`uname`"|MINGW|' configure

%build
# Configure wants to execute a lot of strange stuff.  Feed it cached correct
# values.
%{mingw_configure} \
  --enable-build-mode=production --enable-hl \
  --disable-silent-rules \
  --enable-shared --disable-static \
  --disable-parallel --disable-threadsafe --without-pthread \
  --disable-fortran --disable-cxx --disable-java --disable-tests \
  --with-szlib \
  hdf5_cv_printf_ll=ll \
  hdf5_cv_system_scope_threads=no \
  hdf5_cv_ldouble_to_long_special=no \
  hdf5_cv_long_to_ldouble_special=no \
  hdf5_cv_ldouble_to_llong_accurate=yes \
  hdf5_cv_llong_to_ldouble_correct=yes \
  hdf5_cv_szlib_can_encode=yes \
  hdf5_cv_disable_some_ldouble_conv=no

%mingw_make_build


%install
# Avoid the installation of examples.  It ignores DESTDIR and we would remove
# them from the package later anyway.
sed -i '/^install: /{s/install-examples//}' build_win32/Makefile
sed -i '/^install: /{s/install-examples//}' build_win64/Makefile
%{mingw_make_install}

# Remove .la files
rm %{buildroot}%{mingw32_libdir}/*.la
rm %{buildroot}%{mingw64_libdir}/*.la

# Remove the mirror server
rm %{buildroot}%{mingw32_bindir}/mirror_server.exe
rm %{buildroot}%{mingw32_bindir}/mirror_server_stop.exe
rm %{buildroot}%{mingw64_bindir}/mirror_server.exe
rm %{buildroot}%{mingw64_bindir}/mirror_server_stop.exe

# Make the h5cc shell script a cross-compilation tool.
mkdir -p %{buildroot}%{_bindir}
mv -f %{buildroot}%{mingw32_bindir}/h5cc %{buildroot}%{_bindir}/i686-w64-mingw32-h5cc
mv -f %{buildroot}%{mingw64_bindir}/h5cc %{buildroot}%{_bindir}/x86_64-w64-mingw32-h5cc


%files -n mingw32-hdf5
%{_bindir}/i686-w64-mingw32-h5cc
%{mingw32_bindir}/h5clear.exe
%{mingw32_bindir}/h5copy.exe
%{mingw32_bindir}/h5debug.exe
%{mingw32_bindir}/h5diff.exe
%{mingw32_bindir}/h5dump.exe
%{mingw32_bindir}/h5format_convert.exe
%{mingw32_bindir}/h5import.exe
%{mingw32_bindir}/h5jam.exe
%{mingw32_bindir}/h5ls.exe
%{mingw32_bindir}/h5mkgrp.exe
%{mingw32_bindir}/h5perf_serial.exe
%{mingw32_bindir}/h5redeploy
%{mingw32_bindir}/h5repack.exe
%{mingw32_bindir}/h5repart.exe
%{mingw32_bindir}/h5stat.exe
%{mingw32_bindir}/h5unjam.exe
%{mingw32_bindir}/libhdf5-103.dll
%{mingw32_bindir}/libhdf5_hl-100.dll
%{mingw32_includedir}/H5ACpublic.h
%{mingw32_includedir}/H5api_adpt.h
%{mingw32_includedir}/H5Apublic.h
%{mingw32_includedir}/H5Cpublic.h
%{mingw32_includedir}/H5DOpublic.h
%{mingw32_includedir}/H5Dpublic.h
%{mingw32_includedir}/H5DSpublic.h
%{mingw32_includedir}/H5Epubgen.h
%{mingw32_includedir}/H5Epublic.h
%{mingw32_includedir}/H5FDcore.h
%{mingw32_includedir}/H5FDdirect.h
%{mingw32_includedir}/H5FDfamily.h
%{mingw32_includedir}/H5FDhdfs.h
%{mingw32_includedir}/H5FDlog.h
%{mingw32_includedir}/H5FDmirror.h
%{mingw32_includedir}/H5FDmpi.h
%{mingw32_includedir}/H5FDmpio.h
%{mingw32_includedir}/H5FDmulti.h
%{mingw32_includedir}/H5FDpublic.h
%{mingw32_includedir}/H5FDros3.h
%{mingw32_includedir}/H5FDsec2.h
%{mingw32_includedir}/H5FDsplitter.h
%{mingw32_includedir}/H5FDstdio.h
%{mingw32_includedir}/H5FDwindows.h
%{mingw32_includedir}/H5Fpublic.h
%{mingw32_includedir}/H5Gpublic.h
%{mingw32_includedir}/H5IMpublic.h
%{mingw32_includedir}/H5Ipublic.h
%{mingw32_includedir}/H5LDpublic.h
%{mingw32_includedir}/H5Lpublic.h
%{mingw32_includedir}/H5LTpublic.h
%{mingw32_includedir}/H5MMpublic.h
%{mingw32_includedir}/H5Opublic.h
%{mingw32_includedir}/H5overflow.h
%{mingw32_includedir}/H5PLextern.h
%{mingw32_includedir}/H5PLpublic.h
%{mingw32_includedir}/H5Ppublic.h
%{mingw32_includedir}/H5PTpublic.h
%{mingw32_includedir}/H5pubconf.h
%{mingw32_includedir}/H5public.h
%{mingw32_includedir}/H5Rpublic.h
%{mingw32_includedir}/H5Spublic.h
%{mingw32_includedir}/H5TBpublic.h
%{mingw32_includedir}/H5Tpublic.h
%{mingw32_includedir}/H5version.h
%{mingw32_includedir}/H5Zpublic.h
%{mingw32_includedir}/hdf5.h
%{mingw32_includedir}/hdf5_hl.h
%{mingw32_libdir}/libhdf5.dll.a
%{mingw32_libdir}/libhdf5_hl.dll.a
%{mingw32_libdir}/libhdf5.settings

%files -n mingw64-hdf5
%{_bindir}/x86_64-w64-mingw32-h5cc
%{mingw64_bindir}/h5clear.exe
%{mingw64_bindir}/h5copy.exe
%{mingw64_bindir}/h5debug.exe
%{mingw64_bindir}/h5diff.exe
%{mingw64_bindir}/h5dump.exe
%{mingw64_bindir}/h5format_convert.exe
%{mingw64_bindir}/h5import.exe
%{mingw64_bindir}/h5jam.exe
%{mingw64_bindir}/h5ls.exe
%{mingw64_bindir}/h5mkgrp.exe
%{mingw64_bindir}/h5perf_serial.exe
%{mingw64_bindir}/h5redeploy
%{mingw64_bindir}/h5repack.exe
%{mingw64_bindir}/h5repart.exe
%{mingw64_bindir}/h5stat.exe
%{mingw64_bindir}/h5unjam.exe
%{mingw64_bindir}/libhdf5-103.dll
%{mingw64_bindir}/libhdf5_hl-100.dll
%{mingw64_includedir}/H5ACpublic.h
%{mingw64_includedir}/H5api_adpt.h
%{mingw64_includedir}/H5Apublic.h
%{mingw64_includedir}/H5Cpublic.h
%{mingw64_includedir}/H5DOpublic.h
%{mingw64_includedir}/H5Dpublic.h
%{mingw64_includedir}/H5DSpublic.h
%{mingw64_includedir}/H5Epubgen.h
%{mingw64_includedir}/H5Epublic.h
%{mingw64_includedir}/H5FDcore.h
%{mingw64_includedir}/H5FDdirect.h
%{mingw64_includedir}/H5FDfamily.h
%{mingw64_includedir}/H5FDhdfs.h
%{mingw64_includedir}/H5FDlog.h
%{mingw64_includedir}/H5FDmirror.h
%{mingw64_includedir}/H5FDmpi.h
%{mingw64_includedir}/H5FDmpio.h
%{mingw64_includedir}/H5FDmulti.h
%{mingw64_includedir}/H5FDpublic.h
%{mingw64_includedir}/H5FDros3.h
%{mingw64_includedir}/H5FDsec2.h
%{mingw64_includedir}/H5FDsplitter.h
%{mingw64_includedir}/H5FDstdio.h
%{mingw64_includedir}/H5FDwindows.h
%{mingw64_includedir}/H5Fpublic.h
%{mingw64_includedir}/H5Gpublic.h
%{mingw64_includedir}/H5IMpublic.h
%{mingw64_includedir}/H5Ipublic.h
%{mingw64_includedir}/H5LDpublic.h
%{mingw64_includedir}/H5Lpublic.h
%{mingw64_includedir}/H5LTpublic.h
%{mingw64_includedir}/H5MMpublic.h
%{mingw64_includedir}/H5Opublic.h
%{mingw64_includedir}/H5overflow.h
%{mingw64_includedir}/H5PLextern.h
%{mingw64_includedir}/H5PLpublic.h
%{mingw64_includedir}/H5Ppublic.h
%{mingw64_includedir}/H5PTpublic.h
%{mingw64_includedir}/H5pubconf.h
%{mingw64_includedir}/H5public.h
%{mingw64_includedir}/H5Rpublic.h
%{mingw64_includedir}/H5Spublic.h
%{mingw64_includedir}/H5TBpublic.h
%{mingw64_includedir}/H5Tpublic.h
%{mingw64_includedir}/H5version.h
%{mingw64_includedir}/H5Zpublic.h
%{mingw64_includedir}/hdf5.h
%{mingw64_includedir}/hdf5_hl.h
%{mingw64_libdir}/libhdf5.dll.a
%{mingw64_libdir}/libhdf5_hl.dll.a
%{mingw64_libdir}/libhdf5.settings


%changelog
* Fri Aug 26 2022 Yeti <yeti@gwyddion.net> - 1.10.8-2
- added make as BuildRequires
- F36 rebuild

* Tue Aug 9 2022 Yeti <yeti@gwyddion.net> - 1.10.8-1
- Updated to upstream 1.10.8

* Mon Aug 8 2022 Yeti <yeti@gwyddion.net> - 1.8.20-4
- Downgraded to 1.8.20 as 1.8.22 cannot open files

* Thu Mar 24 2022 Yeti <yeti@gwyddion.net> - 1.8.22-3
- Stop fixing libsz name, it is all right now
- F34 rebuild

* Tue Feb 23 2021 Yeti <yeti@gwyddion.net> - 1.8.22-2
- Stopped trying patch Makefile.in
- F33 rebuild

* Thu Feb 18 2021 Yeti <yeti@gwyddion.net> - 1.8.22-1
- Updated to upstream 1.8.22
- Modernised spec file conventions a bit

* Mon Feb  3 2020 Yeti <yeti@gwyddion.net> - 1.8.20-1
- Created

