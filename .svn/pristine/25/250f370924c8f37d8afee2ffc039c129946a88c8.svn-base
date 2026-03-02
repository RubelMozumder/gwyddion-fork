Name:           gwyddion-release
Version:        39
Release:        1%{?dist}
Summary:        Gwyddion Fedora Repository Configuration

Group:          System Environment/Base
License:        BSD
URL:            http://gwyddion.net/
Source1:        gwyddion.repo
Source2:        gwyddion-mingw.repo
Source3:        RPM-GPG-KEY-gwyddion
BuildArch:      noarch

Requires:       system-release >= 39

%description
The Gwyddion RPM repository contains Gwyddion.  An extra MinGW repo packages
contains packages used for cross-compilation of Gwyddion for MS Windows.


%prep
echo "Nothing to prep"


%build
echo "Nothing to build"


%install
mkdir -p -m 755 \
  $RPM_BUILD_ROOT%{_sysconfdir}/pki/rpm-gpg  \
  $RPM_BUILD_ROOT%{_sysconfdir}/yum.repos.d

%{__install} -p -m 644 %{SOURCE1} $RPM_BUILD_ROOT%{_sysconfdir}/yum.repos.d
%{__install} -p -m 644 %{SOURCE2} $RPM_BUILD_ROOT%{_sysconfdir}/yum.repos.d
%{__install} -p -m 644 %{SOURCE3} $RPM_BUILD_ROOT%{_sysconfdir}/pki/rpm-gpg


%files
%config(noreplace) %{_sysconfdir}/yum.repos.d/gwyddion.repo
%config(noreplace) %{_sysconfdir}/yum.repos.d/gwyddion-mingw.repo
%{_sysconfdir}/pki/rpm-gpg/RPM-GPG-KEY-gwyddion

%changelog
* Tue Jan  9 2024 Yeti <yeti@gwyddion.net> - 39-1
- Fedora 39

* Thu May 18 2023 Yeti <yeti@gwyddion.net> - 38-1
- Fedora 38

* Fri Aug 26 2022 Yeti <yeti@gwyddion.net> - 37-1
- Fedora 37

* Mon May 02 2022 Yeti <yeti@gwyddion.net> - 36-1
- Fedora 36

* Thu Mar 24 2022 Yeti <yeti@gwyddion.net> - 35-1
- Fedora 35

* Fri Nov 12 2021 Yeti <yeti@gwyddion.net> - 34-1
- Fedora 34

* Tue Feb 23 2021 Yeti <yeti@gwyddion.net> - 33-1
- Fedora 33

* Mon Feb 22 2021 Yeti <yeti@gwyddion.net> - 32-2
- Separate MinGW repo with cross-compilation packages no one sane wants

* Fri Jan 08 2021 Yeti <yeti@gwyddion.net> - 32-1
- Fedora 32

* Mon Feb 10 2020 Yeti <yeti@gwyddion.net> - 31-1
- Fedora 31

* Sun Jun  2 2019 Yeti <yeti@gwyddion.net> - 30-1
- Fedora 30

* Tue Nov 27 2018 Yeti <yeti@gwyddion.net> - 29-1
- Fedora 29

* Mon Jul 23 2018 Yeti <yeti@gwyddion.net> - 28-1
- Fedora 28

* Wed Jan 24 2018 Yeti <yeti@gwyddion.net> - 27-1
- Fedora 27

* Mon Aug  7 2017 Yeti <yeti@gwyddion.net> - 26-1
- Fedora 26

* Wed Jan 18 2017 Yeti <yeti@gwyddion.net> - 25-1
- Fedora 25

* Thu Jul 21 2016 Yeti <yeti@gwyddion.net> - 24-1
- Fedora 24

* Sun Dec 20 2015 Yeti <yeti@gwyddion.net> - 23-1
- Fedora 23

* Thu Jul 16 2015 Yeti <yeti@gwyddion.net> - 22-1
- Fedora 22

* Fri Mar 13 2015 Yeti <yeti@gwyddion.net> - 21-1
- Fedora 21

* Sat Dec 21 2013 Yeti <yeti@gwyddion.net> - 20-1
- Fedora 20

* Tue Jul 30 2013 Yeti <yeti@gwyddion.net> - 19-1
- Fedora 19

* Sun Aug  5 2012 Yeti <yeti@gwyddion.net> - 18-1
- Fedora 18

* Sun Aug  5 2012 Yeti <yeti@gwyddion.net> - 17-1
- Created
