#!/usr/bin/python2
import re, sys, os, glob
from collections import Counter

docdirs = (
    'libgwyddion',
    'libgwyprocess',
    'libgwydraw',
    'libgwydgets',
    'libgwymodule',
    'libgwyapp',
)

codedirs = (
    'libgwyddion',
    'libprocess',
    'libdraw',
    'libgwydgets',
    'libgwymodule',
    'app',
    'gwyddion',
    'modules/file',
    'modules/cmap',
    'modules/graph',
    'modules/layer',
    'modules/process',
    'modules/tools',
    'modules/volume',
    'modules/xyz',
)

third_party = 'gwy_version_major gwy_version_minor'.split()
pygwy = 'gwy_xy_new gwy_xyz_new gwy_container_keys gwy_container_keys_by_name gwy_rgba_new gwy_file_save gwy_app_data_id_new'.split()

def update_symbols_from_file(filename, symbols, ext):
    content = open(filename).read()
    content = re.sub(r'\\\n', ' ', content)
    content = re.sub(r'\\"', '', content)
    content = re.sub(r'"[^"]*"', ' ', content)
    content = re.sub(r'(?ms)/\*.*?\*/', '', content)
    content = re.sub(r'(?m)//[^"]*?$', '', content)
    if ext == 'h':
        # Only keep preprocessor stuff
        content = '\n'.join(re.findall(r'(?m)^(#.*?)$', content))
    else:
        # Remove words at the start of the line - these are function definitions.
        content = re.sub(r'(?m)^\w+', ' ', content)
    symbols.update(re.findall(r'\b[A-Za-z_][A-Za-z0-9_]+\b', content))

symbols = Counter()
for subdir in codedirs:
    if '/' in subdir:
        path = subdir.split('/')
    else:
        path = [subdir]

    for ext in 'c', 'cc', 'h':
        p = path + ['*.' + ext]
        for filename in glob.glob(os.path.join(*p)):
            update_symbols_from_file(filename, symbols, ext)

if os.access('utils/used-symbols-external.txt', os.R_OK):
    for filename in open('utils/used-symbols-external.txt').read().split():
        if os.access(filename, os.R_OK):
            update_symbols_from_file(filename, symbols, 'c')
        else:
            sys.stderr.write('File %s no longer exists.\n' % filename)

for subdir in docdirs:
    docsfile = os.path.join('devel-docs', subdir, subdir + '-decl.txt')
    if not os.access(docsfile, os.R_OK):
        continue

    content = open(docsfile).read()
    unused = []
    for m in re.finditer(r'(?ms)<FUNCTION>.*?<NAME>(?P<name>\w+)</NAME>.*?</FUNCTION>', content):
        name = m.group('name')
        n = symbols.get(name, 0)
        if n or name in third_party or name in pygwy:
            continue

        unused.append(name)
    if unused:
        print subdir + ':'
        for name in unused:
            print '    ' + name

# vim: sw=4 ts=4 et:
