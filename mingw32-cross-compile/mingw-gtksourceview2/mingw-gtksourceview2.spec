%{?mingw_package_header}
%define release_version %(echo %{version} | awk -F. '{print $1"."$2}')
%define po_package gtksourceview-2.0

Name:           mingw-gtksourceview2
Version:        2.11.2
Release:        11%{?dist}
Summary:        MinGW Windows library for viewing source files

# the library itself is LGPL, some .lang files are GPL
License:        LGPLv2+ and GPLv2+
Group:          Development/Libraries
URL:            http://www.gtk.org
Source0:        http://download.gnome.org/sources/gtksourceview/%{release_version}/gtksourceview-%{version}.tar.bz2
Patch0:         gtksourceview2-mingw-libtool-win64-lib.patch
Patch1:         gtksourceview-2.11.2-force-gtk2.patch
Patch2:         gtksourceview-2.11.2-deprecations.patch
Patch3:         gtksourceview-2.11.2-notests.patch
Patch4:         gtksourceview-2.11-fix-GCONST-def.patch
Patch5:         gtksourceview-2.11-glib-unicode-constant.patch

BuildArch:      noarch

BuildRequires:  mingw32-filesystem >= 133
BuildRequires:  mingw64-filesystem >= 133
BuildRequires:  mingw32-gcc
BuildRequires:  mingw64-gcc
BuildRequires:  mingw32-binutils
BuildRequires:  mingw64-binutils
BuildRequires:  mingw32-gettext
BuildRequires:  mingw64-gettext
BuildRequires:  mingw32-gtk2
BuildRequires:  mingw64-gtk2
BuildRequires:  mingw32-libxml2
BuildRequires:  mingw64-libxml2
BuildRequires:  mingw32-pkg-config
BuildRequires:  mingw64-pkg-config

BuildRequires:  make
# Native one for msgfmt
BuildRequires:  gettext
# Native one for glib-genmarshal and glib-mkenums
BuildRequires:  glib2-devel
BuildRequires:  intltool

%description
GtkSourceView is a text widget that extends the standard GTK+
GtkTextView widget. It improves GtkTextView by implementing
syntax highlighting and other features typical of a source code editor.

This package contains the MinGW Windows cross compiled GtkSourceView library,
version 2.


%package -n     mingw32-gtksourceview2
Summary:        MinGW Windows library for viewing source files

%description -n mingw32-gtksourceview2
GtkSourceView is a text widget that extends the standard GTK+
GtkTextView widget. It improves GtkTextView by implementing
syntax highlighting and other features typical of a source code editor.

This package contains the MinGW Windows cross compiled GtkSourceView library,
version 2.


%package -n     mingw64-gtksourceview2
Summary:        MinGW Windows library for viewing source files

%description -n mingw64-gtksourceview2
GtkSourceView is a text widget that extends the standard GTK+
GtkTextView widget. It improves GtkTextView by implementing
syntax highlighting and other features typical of a source code editor.

This package contains the MinGW Windows cross compiled GtkSourceView library,
version 2.


%{?mingw_debug_package}


%prep
%setup -q -n gtksourceview-%{version}
%patch0 -p1 -b .win64
%patch1 -p1 -b .gtk2
%patch2 -p1 -b .deprecations
%patch3 -p1 -b .notests
%patch4 -p1 -b .gconst
%patch5 -p1 -b .gunicode
sed -i -e 's/59  *Temple  *Place[- ,]* Suite  *330/51 Franklin Street, Fifth Floor/' \
       -e 's/MA  *02111-1307/MA 02110-1301/' \
       gtksourceview/*.h gtksourceview/completion-providers/words/*.h \
       data/styles/*.xml data/language-specs/*.lang data/language-specs/*.rng \
       data/language-specs/language.dtd data/styles/styles.rng COPYING
sed -i -e 's/\<G_CONST_RETURN\>/const/g' \
       -e 's/\<G_UNICODE_COMBINING_MARK\>/G_UNICODE_SPACING_MARK/g' \
       -e 's/\<gtk_set_locale\>/setlocale/g' \
       gtksourceview/*.[ch]


%build
%{mingw_configure} \
  --disable-static \
  --disable-gtk-doc \
  --enable-deprecations=no \
  --disable-introspection

%{mingw_make_build}


%install
%{mingw_make_install}

# remove unwanted files
rm %{buildroot}%{mingw32_datadir}/gtksourceview-2.0/language-specs/check-language.sh
rm %{buildroot}%{mingw64_datadir}/gtksourceview-2.0/language-specs/check-language.sh
rm %{buildroot}%{mingw32_datadir}/gtksourceview-2.0/styles/check-style.sh
rm %{buildroot}%{mingw64_datadir}/gtksourceview-2.0/styles/check-style.sh

# Remove .la files
rm %{buildroot}%{mingw32_libdir}/*.la
rm %{buildroot}%{mingw64_libdir}/*.la

# Remove documentation that duplicates what's in the native package
rm -rf %{buildroot}%{mingw32_datadir}/gtk-doc
rm -rf %{buildroot}%{mingw64_datadir}/gtk-doc

%{mingw_find_lang} %{po_package}



%files -n mingw32-gtksourceview2 -f mingw32-%{po_package}.lang
%doc COPYING
%{mingw32_bindir}/libgtksourceview-2.0-0.dll
%{mingw32_includedir}/gtksourceview-2.0/
%{mingw32_libdir}/libgtksourceview-2.0.dll.a
%{mingw32_libdir}/pkgconfig/gtksourceview-2.0.pc
%{mingw32_datadir}/gtksourceview-2.0/


%files -n mingw64-gtksourceview2 -f mingw64-%{po_package}.lang
%doc COPYING
%{mingw64_bindir}/libgtksourceview-2.0-0.dll
%{mingw64_includedir}/gtksourceview-2.0/
%{mingw64_libdir}/libgtksourceview-2.0.dll.a
%{mingw64_libdir}/pkgconfig/gtksourceview-2.0.pc
%{mingw64_datadir}/gtksourceview-2.0/



%changelog
* Thu Aug 25 2022 Yeti <yeti@gwyddion.net> - 2.11.2-11
- added make as BuildRequires
- F36 rebuild

* Wed Mar 23 2022 Yeti <yeti@gwyddion.net> - 2.11.2-10
- F34 rebuild

* Thu Feb 18 2021 Yeti <yeti@gwyddion.net> - 2.11.2-9
- Modernised spec file conventions a bit

* Tue Aug  8 2017 Yeti <yeti@gwyddion.net> - 2.11.2-8
- Fixed FSF address

* Sat Dec 21 2013 Yeti <yeti@gwyddion.net> - 2.11.2-7
- Added Fedora patches 4 and 5

* Sat Dec 21 2013 Yeti <yeti@gwyddion.net> - 2.11.2-6
- Force not building tests
- Corrected dates in old changelogs
- Rebuilt for F19

* Tue Dec 17 2013 Yeti <yeti@gwyddion.net> - 2.11.2-5
- Botched configure logic fixed
- Enforce compilation with Gtk+2 even if Gtk+3 is available
- Enforce disabling of deprecation

* Mon Feb 18 2013 Yeti <yeti@gwyddion.net> - 2.11.2-4
- Rebuilt for F18

* Sat Aug  4 2012 Yeti <yeti@gwyddion.net> - 2.11.2-3
- Duplicated all system dependences to include mingw64 variants
- Made build quiet

* Wed Aug  1 2012 Yeti <yeti@gwyddion.net> - 2.11.2-2
- Update to F17 mingw-w64 toolchain and RPM macros
- Build Win64 package
- Added explict cross-pkg-config dependencies, seem needed
- Do not package libtool .la files

* Thu Jul 12 2012 Yeti <yeti@gwyddion.net> - 2.11.2-1
- Initial release

