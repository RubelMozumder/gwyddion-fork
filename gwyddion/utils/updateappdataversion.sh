#!/bin/bash
appdatafile=data/net.gwyddion.Gwyddion.appdata.xml
if test ! -s $appdata; then
    echo 'Run me from the toplevel directory!' 1>&2
    exit 1
fi

release=stable
if test "$1" = development; then
    release=development
elif test "$1" = stable; then
    release=stable
elif test -s "$1"; then
    echo "Release must be development or stable" 1>&2
    exit 1
fi

if test $release = development; then
    if test -z "$2"; then
        echo "Gimme the development version" 1>&2
        exit 1
    fi
    version="$2"
else
    version=$(head -n1 NEWS | cut -d' ' -f1)
fi

date=$(date '+%Y-%m-%d')

fulltag="<release version=\"$version\" date=\"$date\" type=\"$release\" \/>"
sed -i -e "s/<release version=.* date=.*>/$fulltag/" $appdatafile
#svn diff $appdatafile
