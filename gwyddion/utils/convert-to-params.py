#!/usr/bin/python3
import sys, re, pprint

license120 = '''\
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */'''

class Record(object):
    def __repr__(self):
        s = []
        for name in dir(self):
            if name.startswith('_'):
                continue
            s.append('%s=%s' % (name, repr(getattr(self, name))))
        return ', '.join(s)

    def __str__(self):
        s = []
        for name in dir(self):
            if name.startswith('_'):
                continue
            s.append('%s=%s' % (name, str(getattr(self, name))))
        return ', '.join(s)

code = sys.stdin.read()

code = re.sub(r'\b[A-Z_]+_RUN_MODES\b', 'RUN_MODES', code)
code = re.sub(r'\b[A-Z][A-Za-z0-9]+Args\b', r'ModuleArgs', code)
code = re.sub(r'\b[A-Z][A-Za-z0-9]+Controls\b', r'ModuleGUI', code)

code = re.sub(r'\bGwyRunType run\b', r'GwyRunType runtype', code)
code = re.sub(r'\bg_return_if_fail\(run ', r'g_return_if_fail(runtype ', code)
code = re.sub(r'\(run ==', r'(runtype ==', code)

code = re.sub(r'(?m)^\s+gwy_help_add_\w+\(.*?\);\n', '', code)

code = re.sub(r'\bdfield\b', r'field', code)
code = re.sub(r'\bmfield\b', r'mask', code)
code = re.sub(r'\bcontrols\b', r'gui', code)
code = re.sub(r'\bdialogue\b', r'dialog', code)
code = re.sub(r'\b\w*dialog\(', r'run_gui(', code)

code = re.sub(r'(?ms)static gboolean\nrun_gui\(', r'static GwyDialogOutcome\nrun_gui(', code)

m = re.search(r'(?ms)^static void\n\w+\(GwyContainer\s+\*\w+,\s*GwyRunType\s+.*?^}', code)
if m:
    main = m.group()
    main = re.sub(r'(?m)^\s+GwyContainer \*settings;\n', '', main)
    main = re.sub(r'(?m)^\s+GwyContainer \*settings = gwy_app_settings_get\(\);\n', '', main)
    main = re.sub(r'\b\w+load_args\([^,]+, &args\);',
                  r'args.params = gwy_params_new_from_settings(define_module_params());',
                  main)
    main = re.sub(r'\w+save_args\([^,]+, &args\);', r'gwy_params_save_to_settings(args.params);', main)
    main = re.sub(r'(?m)\bgboolean ok;', 'GwyDialogOutcome outcome;', main)
    main = re.sub(r'ok = run_gui\(', 'outcome = run_gui(', main)
    main = re.sub(r'}\Z', '\nend:\n    g_object_unref(args.result);\n    g_object_unref(args.params);\n}', main)
    code = code[:m.start()] + main + code[m.end():]

vimline = '/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */\n'
code = re.sub(r'(?ms)^/\* vim: .*? \*/\s*', vimline, code)
code = re.sub(r'(?ms)^ \*  This program is free software;.*?\*/', license120, code)
code = re.sub(r'(Copyright \(C\) 20[0-9]{2})(-20[0-9]{2})?', r'\1-2022', code)

m = re.search(r'(?ms)^typedef struct \{(?P<struct>.*?)\} ModuleArgs;', code)
assert m
structnames = set(mm.group(1) for mm in re.finditer(r'(\w+);', m.group('struct')))

parnames = []
for m in re.finditer(r'static const gchar (?P<kname>\w+)_key\[\]\s+= "/module/[a-z0-9-]+/(?P<mname>[a-z0-9-]+)";', code):
    kname = m.group('kname')
    mname = m.group('mname')
    if kname in structnames:
        parnames.append(kname)
    else:
        parnames.append(mname)

paraminfo = {}

for m in re.finditer(r'\bgwy_container_(set|get|gis)_(?P<type>\w+)_by_name\([^,]+,\s+(?P<kname>\w+)_key,\s+&?\w+->(?P<sname>\w+)\);', code):
    info = Record()
    info.ptype = m.group('type')
    info.sname = m.group('sname')
    info.kname = m.group('kname')
    paraminfo[info.kname] = paraminfo[info.sname] = info

for m in re.finditer(r'\bgwy_app_data_id_verify_(?P<what>\w+)\(&\w+->(?P<sname>\w+)\);', code):
    info = Record()
    info.sname = info.kname = m.group('sname')
    what = m.group('what')
    if what == 'channel':
        what = 'image'
    info.ptype = what + '_id'
    paraminfo[info.kname] = paraminfo[info.sname] = info
    parnames.append(info.sname)

if re.search(r'\bcreate_mask_color_button\s*\(', code):
    info = Record()
    info.kname = info.sname = None
    info.ptype = 'mask_color'
    paraminfo['mask_color'] = info
    parnames.append('mask_color')

tableattach = {}
for m in re.finditer(r'(?ms)\b(gtk_table_attach|gwy_table_attach_\w+)\((?P<args>[^;]+)\);', code):
    label = None
    name = None
    for arg in m.group('args').split(','):
        arg = arg.strip()
        if re.search(r'^(gwy_sgettext|_)\("', arg) and not arg[arg.find('"')+1].islower():
            label = arg
        else:
            m = re.search(r'^(GTK_OBJECT\()?(.?controls\|.?args|gui)(\.|->)(?P<name>\w+)\)?$', arg)
            if m:
                name = m.group('name')
    if label and name:
        tableattach[name] = label.replace(':")', '")')

for m in re.finditer(r'(?ms)^\s+\w+(\.|->)(?P<name>\w+)\s*=\s*gtk_check_button_new\w*\((?P<label>_\(".*?"\))\);', code):
    tableattach[m.group('name')] = re.sub(r'(?s)"\s+"', r'', m.group('label'))

for name, info in paraminfo.items():
    if info.ptype in ('int32', 'double'):
        info.min = 0.0
        info.max = 1.0
        m = re.search(r'(?ms)gtk_adjustment_new\(args->' + info.sname + r',\s*(?P<args>[^;]+)\);', code)
        if m is None:
            m = re.search(r'(?ms)args->' + info.sname + r'\s+=\s+CLAMP\(args->' + info.sname + r',(?P<args>[^;]+)\);', code)
        if m:
            args = m.group('args').split(',')
            try:
                info.min = float(args[0])
            except ValueError:
                pass
            try:
                info.max = float(args[1])
            except ValueError:
                pass

parenum = []
parenum.append('enum {')
for p in parnames:
    parenum.append('    PARAM_' + p.upper() + ',')
parenum.append('};')
parenum = '\n'.join(parenum)

gwyenums = []
for m in re.finditer(r'(?ms)^\s+static const GwyEnum \w+.*?;', code):
    enum = m.group()
    if not enum.startswith(' '):
        enum = re.sub(r'(?ms)^', '    ', enum)
    gwyenums.append(enum.rstrip())

for m in re.finditer(r'(?ms)^\s+\w+(\.|->)(?P<name>\w+)\s*=\s*(?:gwy_radio_buttons_createl|gwy_enum_combo_box_newl)\((?P<args>[^;]*)\);', code):
    name = m.group('name')
    args = m.group('args').split(',')
    enum = ['    static const GwyEnum %ss[] = {' % name]
    for i, arg in enumerate(args):
        arg = arg.strip()
        if not re.search(r'^(gwy_sgettext|_)\("', arg):
            continue
        arg = re.sub(r'^\w+', 'N_', arg)
        enum.append('        { %s, %s },' % (arg, args[i+1].strip()))
    enum.append('    };')
    gwyenums.append('\n'.join(enum))

pardef_prologue = '''\
static GwyParamDef*
define_module_params(void)
{'''
pardef_body='''\
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());'''

pardef_epilogue = '''\
    return paramdef;
}'''

pardef = []
pardef.append(pardef_prologue)
pardef.append('\n'.join(gwyenums))
pardef.append(pardef_body)
for p in parnames:
    pupper = 'PARAM_' + p.upper()
    if p not in paraminfo:
        pardef.append('    gwy_param_def_add_(paramdef, %s, %s, _(""), ...);' % (pupper, p))
        continue
    info = paraminfo[p]
    if info.kname is None:
        key = 'NULL'
    else:
        key = '"%s"' % info.kname
    intro = 'paramdef, %s, %s, _("")' % (pupper, key)
    if info.ptype == 'enum':
        if info.kname in ('masking',):
            intro = intro.replace('_("")', 'NULL')
            pardef.append('    gwy_param_def_add_enum(%s, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);' % (intro,))
        elif info.kname in ('interp', 'interpolation'):
            intro = intro.replace('_("")', 'NULL')
            pardef.append('    gwy_param_def_add_enum(%s, GWY_TYPE_INTERPOLATION_TYPE, GWY_INTERPOLATION_LINEAR);' % (intro,))
        elif info.kname in ('window', 'windowing'):
            intro = intro.replace('_("")', 'NULL')
            pardef.append('    gwy_param_def_add_enum(%s, GWY_TYPE_WINDOWING_TYPE, GWY_WINDOWING_HANN);' % (intro,))
        else:
            if p in tableattach:
                intro = intro.replace('_("")', tableattach[p])
            lcname = p.lower().replace('_', '')
            pardef.append('    gwy_param_def_add_gwyenum(%s,' % (intro,))
            pardef.append('                              %ss, G_N_ELEMENTS(%ss), ...);' % (lcname, lcname))
        continue
    if p in tableattach:
        intro = intro.replace('_("")', tableattach[p])
    if info.ptype == 'double':
        def format_g(x):
            s = '%g' % x
            if '.' not in s:
                return s + '.0'
            return x
        pardef.append('    gwy_param_def_add_double(%s, %s, %s, ...);' % (intro, format_g(info.min), format_g(info.max)))
    elif info.ptype == 'int32':
        pardef.append('    gwy_param_def_add_int(%s, %.0f, %.0f, ...);' % (intro, info.min, info.max))
    elif info.ptype == 'boolean':
        if info.kname in ('update', 'instant_update'):
            intro = re.sub(r'_\("[^"]*"\)', 'NULL', intro)
            pardef.append('    gwy_param_def_add_instant_updates(%s, ...);' % (intro,))
        else:
            pardef.append('    gwy_param_def_add_boolean(%s, ...);' % (intro,))
    elif info.ptype == 'graph_id':
        intro = intro.replace('_("")', 'NULL')
        pardef.append('    gwy_param_def_add_target_graph(%s, NULL);' % (intro,))
    elif info.ptype == 'image_id':
        pardef.append('    gwy_param_def_add_image_id(%s);' % (intro,))
    elif info.ptype == 'mask_color':
        intro = intro.replace('_("")', 'NULL')
        pardef.append('    gwy_param_def_add_mask_color(%s);' % (intro,))
    elif info.ptype == 'const_string':
        intro = intro.replace('_("")', 'NULL')
        pardef.append('    gwy_param_def_add_unit(%s);' % (intro,))
    else:
        sys.stderr.write(info.ptype + '\n')
        assert not 'Reached'
pardef.append(pardef_epilogue)
pardef = '\n'.join(pardef)

m = re.search(r'typedef struct \{', code)
if m:
    code = code[:m.start()] + parenum + '\n\n' + code[m.start():]

m = re.search(r'(?ms)^module_register\([^{}]*\{[^{}]*\}', code)
if m:
    code = code[:m.end()] + '\n\n' + pardef + code[m.end():]

standard_structs = '''\
typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;
'''

func_declarations = '''\
static GwyParamDef*     define_module_params(void);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
'''

m = re.search(r'(?ms)^static gboolean\s+module_register\s*\([^)]*\);\s+', code)
if m:
    code = code[:m.start()] + standard_structs + '\n' + m.group() + func_declarations + code[m.end():]

sys.stdout.write(code)
