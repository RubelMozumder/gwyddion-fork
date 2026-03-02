#!/usr/bin/python2
# $Id: pygwy-generate-doc.py 21886 2019-02-15 13:14:48Z yeti-dn $
# Script for generation of gwy.py dummy file. The file contains empty function bodies with comments.
# Used for generation of documentation by using epydoc.
# Public domain

import re, sys, string
from optparse import OptionParser

parser = OptionParser(usage='''\
pygwy-generate-doc.py --codegendir=DIR >gwy.py
Reads pygwy.defs and pygwywrap.c.
Generates dummy module gwy.py that has the same API and function documentation
extracted from C.''')

parser.add_option('-c', '--codegendir', dest='codegendir', type='string',
                  default='', metavar='DIR',
                  help='Directory with codegen python modules')
parser.add_option('-u', '--unimplemented', dest='unimplemented',
                  action='store_true', default=False,
                  help='Write unimplemented methods with z_UNIMPLEMENTED_ prefix')

(options, args) = parser.parse_args()
if options.codegendir:
    sys.path.insert(0, options.codegendir)
assert not args

import docextract, defsparser, override, definitions, string

# interesting variables
doc_dirs = ['../../' + x for x in 'app libdraw libgwyddion libgwydgets libgwymodule libprocess'.split()] + ['.']
defs_file = 'pygwy.defs'
wrap_file = 'pygwywrap.c'
ignore_functions = ['static', 'typedef', 'be', 'parameter']
keywords = 'as assert def exec from global import in is lambda pass print raise with yield'.split()
ignore_endswith = ['_pygwy']
indent = '    '

datatypes = {
    'gdouble': 'float',
    'GwyDoubleOutArg': 'float',
    'gboolean': 'bool',
    'gchar*': 'string',
    'const-gchar*': 'string',
    'const-guchar*': 'string',
    'gint': 'int',
    'guint': 'int',
    'gint32': 'int',
    'GwyIntOutArg': 'int',
    'GQuark': 'int',
    'GString*': 'string',
    'GwyIntArray*': 'list',
    'GwyDoubleArray*': 'list',
    'GwyIntArrayOutArg': 'list',
    'GwyDoubleArrayOutArg': 'list',
    'GwyDataFieldArray*': 'list',
    'GObject*': 'L{gobject.GObject}',
}

def unwrap_name(fname, fail_if_not_wrapped=False):
    if fname.endswith('_pygwy'):
        return fname[:-6]
    if fail_if_not_wrapped:
        assert fname.endswith('_pygwy')
    return fname

def get_python_name(c_name):
    pymap = {
        'xy': 'XY', 'xyz': 'XY', 'rgba': 'RGBA', 'math_nlfit': 'MathNLFit',
        'gl_material': 'GLMaterial', 'hruler': 'HRuler', 'vruler': 'VRuler',
        'hmarker': 'HMarker', 'nl_fitter': 'NLFitter', 'si_unit': 'SIUnit',
        'cdline': 'CDLine', 'nl_fit_preset': 'NLFitPreset',
    }
    py_name = c_name.capitalize()
    for c, p in pymap.items():
        if c_name == 'gwy_' + c:
            py_name = 'Gwy' + p
            break
    letters = list(re.findall(r'_[a-z]', c_name))
    for match in letters:
        py_name = py_name.replace(match, match.upper()[1])
    return py_name.replace('_', '')

def get_c_name(py_name):
    c_name = re.sub(r'([A-Z][a-z]+)', r'\1_', py_name)
    c_name = re.sub(r'([A-Z]{2,})([A-Z][a-z])', r'\1_\2', c_name)
    return c_name.lower().strip('_')

def found_in_texts(s, *args):
    for a in args:
        if a.find(s) != -1:
            return True
    return False

def rep_code(r):
    s = r.group(0)
    if r.group(1) in ('param', 'return', 'cvar', 'note', 'warning', 'attention'):
        return s
    return 'B{C{' + s[1:] + '}}'

def rep_location(r):
    return 'L{' + r.group(0) + '}'

def replace_special(s):
    s = re.sub(r'<warning>', r'@warning:', s)
    s = re.sub(r'</warning>', r'', s)
    s = re.sub(r'<(emphasis|guimenu|guimenuitem)>(.*?)</\1>', r'I{\2}', s)
    s = re.sub(r'<literal>(.*?)</literal>', r'C{\1}', s)
    s = re.sub(r'<!--\s+-->', r'', s)
    s = re.sub(r'%([a-zA-Z0-9_]+)', rep_code, s)
    s = re.sub(r'@([a-zA-Z0-9_]+)', rep_code, s)
    s = re.sub(r'#([a-zA-Z0-9_]+)', rep_code, s)
    s = re.sub(r'\bTRUE\b', r'True', s)
    s = re.sub(r'\bFALSE\b', r'False', s)
    s = re.sub(r'\bNULL\b', r'None', s)
    s = re.sub(r'\bGWY_', r'', s)
    s = re.sub(r'\bGTK_', r'gtk.', s)
    s = re.sub(r'\bGDK_', r'gdk.', s)
    s = re.sub(r'\bG_', r'glib.', s)
    p = re.compile(r'(gwy_[a-zA-Z0-9_]+)')
    s = re.sub(p, rep_location, s)
    for c_class in c_class_list:
        s = re.sub(c_class + '_', get_python_name(c_class) + '.', s)
    s = re.sub(r'([a-z]\.)([1-3]d)([a-z]+)', r'\1\3\2', s)
    s = re.sub(r'\bGwy([A-Z0-9])', r'\1', s)
    s = re.sub(r'\bGWY_', '', s)
    s = re.sub(r'(?m)^Since: ', '@since: ', s)
    return s

def format_datatype(ptype, datatypes):
    return 'I{(' + datatypes.get(ptype, ptype) + ')}'

def printdoc(s):
    print replace_special(re.sub(r'\A\s+', r'', s))

def printcode(s='', level=0):
    if s:
        print indent*level + s
    else:
        print

def print_function(level, method, docs, enumvals, isconstr=False):
    # Gather all out-arguments and exclude them from the normal parameter list.
    allretvals = []
    if (method.ret is None or method.ret in ('none', 'GwyArrayFuncStatus')):
        normal_retval = False
    else:
        normal_retval = True
        # FIXME: We have no idea how to call the return value here.
        allretvals.append(definitions.Parameter(method.ret, 'value',
                                                None, None))

    # Prototype line
    ignored_params = set()
    protoparams = []
    for i, p in enumerate(method.params):
        pname = p.pname
        if pname in keywords:
            pname += '_'
        if p.ptype == 'GError**':
            ignored_params.add(pname)
            continue
        if (p.ptype.startswith('Gwy') and p.ptype.endswith('OutArg')
            or p.ptype == 'GwySkipArg'):
            allretvals.append(p)
            ignored_params.add(pname)
            continue
        protoparams.append(pname)
    printcode('def ' + method.name + '(' + ', '.join(protoparams) + '):', level)

    # Docstring.  Take it from _pygwy wrapped variant if it has manually
    # written documentation. Otherwise try the original function.  This means
    # we have to do it in the reverse order.
    doc = docs.get(unwrap_name(method.c_name), None)
    doc = docs.get(method.c_name, doc)
    if doc:
        printcode('"""', level+1)
        printdoc(doc.description)
        # Go through actual method params (from defs file).
        for p in method.params:
            pname = p.pname
            if pname in keywords:
                pname += '_'
            if pname in ignored_params:
                continue

            # Try to find the corresponding documentation.
            param = [x for x in doc.params if x[0] == pname]
            if len(param) > 1:
                sys.stderr.write('Parameter %s of %s found multiple times.\n'
                                 % (p.pname, c_name))
                continue
            paramdoc = param[0][1].rstrip() + ' ' if param else ''

            # Check for enum and write a list of expected values.
            if p.ptype in enumvals:
                expvals = enumvals[p.ptype]
                ptype = 'I{(L{' + p.ptype + '})}'
            else:
                expvals = ''
                ptype = format_datatype(p.ptype, datatypes)
            printdoc('@param ' + pname + ': ' + paramdoc + expvals + ptype)

        # Handle methods/functions returning tuples at least somehow.
        retdoc = doc.ret
        if type(retdoc) is not str:
            retdoc = retdoc[0]
        retdoc = retdoc.strip()

        if len(allretvals) == 1 and normal_retval:
            ptype = format_datatype(method.ret, datatypes)
            printdoc('@return: ' + retdoc + ' ' + ptype)
        elif len(allretvals) == 1 and not normal_retval:
            retname = 'B{C{' + allretvals[0].pname + '}}'
            rettype = format_datatype(allretvals[0].ptype, datatypes)
            printdoc('@return: Value ' + retname + '. (' + rettype + ')')
        elif allretvals:
            retnames = ', '.join('B{C{' + p.pname + '}}' for p in allretvals)
            rettypes = ', '.join(format_datatype(p.ptype, datatypes)
                                 for p in allretvals)
            printdoc('@return: Tuple consisting of ' + str(len(allretvals))
                     + ' values (' + retnames + '). (' + rettypes + ')')

        printcode('"""', level+1)

    if allretvals:
        ret = ', '.join('None' for i in range(len(allretvals)))
        printcode('return ' + ret, level+1)
    else:
        printcode('pass', level+1)
    printcode()

def print_class(obj, docs, sectionmap):
    printcode()
    printcode('class %s:' % obj.name)
    if docs.has_key(obj.c_name):
        printcode('"""', 1)
        if obj.c_name in sectionmap:
            name = sectionmap[obj.c_name]
            for param in docs[name].params:
                if param[0] == 'short_description':
                    printdoc(param[1])
                    printcode()
            printdoc(docs[name].description)
        else:
            printdoc(docs[obj.c_name].description)
        printcode('"""', 1)

# Patch docextract's skip_to_identifier() because we want to recognise:
# - types (mainly enums, but possibly other stuff)
# - sections (for class documentation for which C documentation is useless)
identifier_patterns = docextract.identifier_patterns
type_name_pattern = re.compile(r'^(Gwy\w*)\s*:?(\s*\(.*\)\s*){0,2}\s*$')
section_name_pattern = re.compile(r'^(SECTION:\s*\w+):?(\s*\(.*\)\s*){0,2}\s*$')
identifier_patterns = (
    ('section', section_name_pattern),
    ('signal', docextract.signal_name_pattern),
    ('property', docextract.property_name_pattern),
    ('function', docextract.function_name_pattern),
    ('type', type_name_pattern),
)

def skip_to_identifier(fp, line, cur_doc):
    # Skip the initial comment block line ('/**') if not eof.
    if line:
        line = fp.readline()

    # Now skip empty lines.
    line = docextract.skip_to_nonblank(fp, line)

    # See if the first non-blank line is the identifier.
    if line and not docextract.comment_end_pattern.match(line):
        # Remove the initial ' * ' in comment block line and see if there is an
        # identifier.
        line = docextract.comment_line_lead_pattern.sub('', line)
        for pattern in identifier_patterns:
            match = pattern[1].match(line)
            if match:
                cur_doc.set_name(match.group(1))
                annotations = docextract.get_annotation_list(match.group(2))
                for annotation in annotations:
                    cur_doc.add_annotation(annotation)
                cur_doc.set_type(pattern[0])
                return line
    return line

docextract.skip_to_identifier = skip_to_identifier

docs = docextract.extract(doc_dirs)
sectionmap = [s for s in docs.keys() if s.startswith('SECTION:')]
for i, s in enumerate(sectionmap):
    param = [x[1] for x in docs[s].params if x[0] == 'title']
    if len(param) == 1 and param[0].strip():
        sectionmap[i] = (param[0].strip(), s)
sectionmap = dict(sectionmap)

parser = defsparser.DefsParser(defs_file)
parser.startParsing()
parser.include('override.defs')

# Create list of class.  Add rewrite rules GwyWhatever* -> L{Whatever}
c_class_list = []
for obj in parser.objects + parser.boxes:
    c_class_list.append(get_c_name(obj.c_name))
    datatypes[obj.c_name + '*'] = 'L{' + obj.name + '}'

# Add rewrite rules GtkWhatever* -> L{gtk.Whatever}
for line in file(defs_file):
    for m in re.finditer(r'"(Gtk|Gdk)([A-Z]\w+)\*"', line):
        prefix, suffix = m.group(1), m.group(2)
        datatypes[prefix + suffix + '*'] = 'L{' + prefix.lower() + '.' + suffix + '}'

# Create helper dict for listings of enumerated values.
expected_enumvals = dict()
for enum in parser.enums:
    expvals = ['C{B{' + re.sub(r'^GWY_', r'', enumval[1]) + '}}'
               for enumval in enum.values]
    expvals = ' Expected values: ' + ', '.join(expvals) + '. '
    expected_enumvals[enum.c_name] = expvals

#add_override_methods(parser)

f = file(wrap_file)
wrap_file_cont = f.read()
override_file_cont = ''
override = override.Overrides()

# Keep GENERATED seprarated so that this file is not marked generated.
print '# vim: set fileencoding=utf-8 :'
print '# This is dummy %s file used for generation of documentation' % 'GENERATED'

# Objects/classes
for obj in parser.objects + parser.boxes:
    class_written = False
    any_methods = False
    if wrap_file_cont.find(obj.name) == -1:
        sys.stderr.write('Class %s not found.\n' % obj.c_name)
        continue

    constructor = parser.find_constructor(obj, override)
    if constructor:
        if not class_written:
            print_class(obj, docs, sectionmap)
            class_written = True
        ignore_functions.append(constructor.name)
        constructor.name = '__init__'
        print_function(1, constructor, docs, expected_enumvals,
                       isconstr=True)
        any_methods = True
    for method in parser.find_methods(obj):
        method.name = unwrap_name(method.name)
        if method.varargs:
            continue
        if not class_written:
            print_class(obj, docs, sectionmap)
            class_written = True
        if not found_in_texts(method.name, wrap_file_cont, override_file_cont):
            sys.stderr.write('Method %s not found.\n' % method.c_name)
            if not options.unimplemented:
                continue
            method.name = 'UNIMPLEMENTED_' + method.name
        print_function(1, method, docs, expected_enumvals)
        any_methods = True

    if class_written and not any_methods:
        printcode('pass', 1)

# Functions
for func in sorted(parser.functions, key=lambda x: x.name):
    if (isinstance(func, definitions.MethodDef)
        or func.is_constructor_of
        or func.varargs
        or func.name in ignore_functions):
        continue
    func.name = unwrap_name(func.name)
    # check if function is used in pygwywrap.c file
    if not found_in_texts(func.name, wrap_file_cont, override_file_cont):
        sys.stderr.write('Function %s not found.\n' % func.c_name)
        if not options.unimplemented:
            continue
        func.name = 'z_UNIMPLEMENTED_' + func.name
    print_function(0, func, docs, expected_enumvals)

# Enums
for enum in parser.enums:
    printcode()
    printcode('class %s:' % enum.name)
    printcode('"""', 1)
    if docs.has_key(enum.c_name):
        printdoc(docs[enum.c_name].description)
        params = docs[enum.c_name].params
    printdoc('''\
@note: All the enumerated values are defined at the module level, I{not} the
class level.  The are just groupped by class here for easier orientation.
''')
    for enumval in enum.values:
        c_name = enumval[1]
        pyname = re.sub(r'^GWY_', r'', c_name)
        doc = ''
        for paramdoc in params:
            if paramdoc[0] == c_name:
                doc = paramdoc[1].strip()
                break
        printdoc('@cvar %s: %s' % (pyname, doc))
    printcode('"""', 1)
    printcode('pass', 1)

