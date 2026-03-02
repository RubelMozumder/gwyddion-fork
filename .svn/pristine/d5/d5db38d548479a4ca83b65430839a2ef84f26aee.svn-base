dnl $1 = semantic program version
dnl $2 = wanted (minimum) semantic version
dnl $3 = action if version is satisfied
dnl $4 = action if version is not satisfied

AC_DEFUN([GWY_VERSION_IFELSE],
[# Kill anything that goes after any letter in the version.  Crude, but usually
# OK.
ver=$(echo "$1" | sed -e 's/@<:@a-zA-Z@:>@.*//')
wver=$(echo "$2" | sed -e 's/@<:@a-zA-Z@:>@.*//')
result=same
while test "x$ver" != x; do
  v=$(echo "$ver" | sed -e 's/\..*//')
  w=$(echo "$wver" | sed -e 's/\..*//')
  if test $v -lt $w; then
    result=smaller
    break
  fi
  if test $v -gt $w; then
    result=larger
    break
  fi
  # Remove the number and dot in two separate steps.  This removes also the
  # last dotless number.
  ver=$(echo "$ver" | sed -e 's/^@<:@^.@:>@*//' -e 's/^\.//')
  wver=$(echo "$wver" | sed -e 's/^@<:@^.@:>@*//' -e 's/^\.//')
done
if test $result = smaller; then
  $4
  :
else
  $3
  :
fi])
