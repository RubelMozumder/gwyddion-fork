#!/bin/sh

out="$1"
shift

echo '/* This is a 'GENERATED' file. */' >"$out.tmp"

cat >>"$out.tmp" <<'EOF'
#include <glib.h>
#include <libgwymodule/gwymodule.h>

static const GwyModuleRecord* register_bundle(void);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION | GWY_MODULE_BUNDLE_FLAG,
    (GwyModuleRegisterFunc)&register_bundle,
    NULL, NULL, NULL, NULL, NULL,
};

GWY_MODULE_QUERY(module_info)

EOF

modnames=$(echo "$@" | sed -e 's/\.la  */ /g' -e 's/\.la$//')
for modname in $modnames; do
    echo $modname
done | sed -e 'y/-/_/' \
           -e 's/\(.*\)/GwyModuleInfo* _gwy_module_query__\1(void);/' \
           >>"$out.tmp"

cat >>"$out.tmp" <<'EOF'

static const GwyModuleRecord modules[] = {
EOF

# FIXME: How to do this in portable shell & sed without calling sed on every
# single modname?
for modname in $modnames; do
    cname=$(echo "$modname" | sed -e 'y/-/_/')
    echo "  { _gwy_module_query__$cname, \"$modname\", },"
done >>"$out.tmp"

cat >>"$out.tmp" <<'EOF'
  { NULL, NULL, },
};

static const GwyModuleRecord*
register_bundle(void)
{
    return modules;
}
EOF

cat "$out.tmp" >"$out"
rm -f "$out.tmp"
