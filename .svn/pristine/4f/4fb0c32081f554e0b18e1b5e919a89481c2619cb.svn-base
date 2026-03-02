%{?mingw_package_header}

# Patch version?
%global snaprel %{nil}

Name:           mingw-libaec
Version:        1.0.6
Release:        2%{?dist}
Summary:        MinGW Windows Adaptive Entropy Coding library
License:        BSD

Url:            https://gitlab.dkrz.de/k202009/libaec
Source0:        https://gitlab.dkrz.de/k202009/libaec/-/archive/v%{version}/libaec-v%{version}.tar.gz

BuildArch:      noarch

BuildRequires:  mingw32-filesystem >= 133
BuildRequires:  mingw64-filesystem >= 133
BuildRequires:  mingw32-gcc
BuildRequires:  mingw64-gcc
BuildRequires:  mingw32-binutils
BuildRequires:  mingw64-binutils
BuildRequires:  cmake >= 3.1
BuildRequires:  make


%description
Libaec provides fast loss-less compression of 1 up to 32 bit wide
signed or unsigned integers (samples). The library achieves best
results for low entropy data as often encountered in space imaging
instrument data or numerical model output from weather or climate
simulations. While floating point representations are not directly
supported, they can also be efficiently coded by grouping exponents
and mantissa.

Libaec implements Golomb Rice coding as defined in the Space Data
System Standard documents 121.0-B-2 and 120.0-G-2.

Libaec includes a free drop-in replacement for the SZIP
library (http://www.hdfgroup.org/doc_resource/SZIP).

This package contains the MinGW Windows cross-compiled libaec.

%package -n     mingw32-libaec
Summary:        MinGW Windows Adaptive Entropy Coding library

%description -n mingw32-libaec
Libaec provides fast loss-less compression of 1 up to 32 bit wide
signed or unsigned integers (samples). The library achieves best
results for low entropy data as often encountered in space imaging
instrument data or numerical model output from weather or climate
simulations. While floating point representations are not directly
supported, they can also be efficiently coded by grouping exponents
and mantissa.

Libaec implements Golomb Rice coding as defined in the Space Data
System Standard documents 121.0-B-2 and 120.0-G-2.

Libaec includes a free drop-in replacement for the SZIP
library (http://www.hdfgroup.org/doc_resource/SZIP).

This package contains the MinGW Windows cross-compiled libaec.

%package -n     mingw64-libaec
Summary:        MinGW Windows Adaptive Entropy Coding library

%description -n mingw64-libaec
Libaec provides fast loss-less compression of 1 up to 32 bit wide
signed or unsigned integers (samples). The library achieves best
results for low entropy data as often encountered in space imaging
instrument data or numerical model output from weather or climate
simulations. While floating point representations are not directly
supported, they can also be efficiently coded by grouping exponents
and mantissa.

Libaec implements Golomb Rice coding as defined in the Space Data
System Standard documents 121.0-B-2 and 120.0-G-2.

Libaec includes a free drop-in replacement for the SZIP
library (http://www.hdfgroup.org/doc_resource/SZIP).

This package contains the MinGW Windows cross-compiled libaec.


%{?mingw_debug_package}


%prep
%setup -q -n libaec-v%{version}

%build
MINGW32_CMAKE_ARGS="
	-DLIB_DESTINATION:PATH=%{mingw32_libdir}"

MINGW64_CMAKE_ARGS="
	-DLIB_DESTINATION:PATH=%{mingw64_libdir}"

%mingw_cmake -DBUILD_STATIC_LIBRARIES:BOOLEAN=FALSE

%mingw_make_build


%install
%{mingw_make} install/fast DESTDIR=$RPM_BUILD_ROOT

# Remove man pages
rm -rf %{buildroot}%{mingw32_mandir}/man1
rm -rf %{buildroot}%{mingw64_mandir}/man1


%files -n mingw32-libaec
%{mingw32_bindir}/libsz.dll
%{mingw32_bindir}/libaec.dll
%{mingw32_bindir}/aec.exe
%{mingw32_includedir}/szlib.h
%{mingw32_includedir}/libaec.h
%{mingw32_libdir}/libaec.dll.a
%{mingw32_libdir}/libsz.dll.a
%{mingw32_libdir}/libaec.a
%{mingw32_libdir}/libsz.a
%{mingw32_prefix}/cmake/libaec-config.cmake
%{mingw32_prefix}/cmake/libaec-config-version.cmake

%files -n mingw64-libaec
%{mingw64_bindir}/libsz.dll
%{mingw64_bindir}/libaec.dll
%{mingw64_bindir}/aec.exe
%{mingw64_includedir}/szlib.h
%{mingw64_includedir}/libaec.h
%{mingw64_libdir}/libaec.dll.a
%{mingw64_libdir}/libsz.dll.a
%{mingw64_libdir}/libaec.a
%{mingw64_libdir}/libsz.a
%{mingw64_prefix}/cmake/libaec-config.cmake
%{mingw64_prefix}/cmake/libaec-config-version.cmake


%changelog
* Thu Aug 25 2022 Yeti <yeti@gwyddion.net> - 1.0.6-2
- F36 rebuild

* Thu Mar 24 2022 Yeti <yeti@gwyddion.net> - 1.0.6-1
- Updated to 1.0.6
- F34 rebuild

* Thu Feb 18 2021 Yeti <yeti@gwyddion.net> - 1.0.4-2
- Modernised spec file conventions a bit

* Mon Feb  3 2020 Yeti <yeti@gwyddion.net> - 1.0.4-1
- Created

