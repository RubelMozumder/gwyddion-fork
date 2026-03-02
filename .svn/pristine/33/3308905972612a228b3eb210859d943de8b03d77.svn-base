#!/usr/bin/python2
# Update source code to match stock icons listed in pixmaps/Makefile.am
import re, os, sys, subprocess

base = 'libgwydgets/gwystock'
editorfile = 'gwyddion/toolbox-editor.c'
makefile = 'pixmaps/Makefile.am'

def read_images(f, target='list-icons'):
    """Read image list by running make list-icons."""
    images = {}
    command = ['make', '-C', 'pixmaps', target]
    lst = subprocess.check_output(command)
    for line in lst.split('\n'):
        line = line.strip()
        if not line.startswith('ICONLIST'):
            continue
        icon_re = re.compile(r'^gwy_(?P<name>[a-z0-9_]+)-(?P<size>[0-9]+)\.png$')
        for icon in line.split()[1:]:
            m = icon_re.search(icon)
            if m:
                images.setdefault(m.group('name'), [])
                images[m.group('name')].append(int(m.group('size')))
    return images

def read_since(f):
    """Read `Since' notes from Makefile"""
    sinces = {}
    for line in file(f):
        m = re.match(r'^\s*#\s+Since\s+(?P<version>[0-9.]+):\s*(?P<list>[-a-z0-9_. ]+)', line)
        if not m:
            continue
        for i in m.group('list').split():
            assert i not in sinces
            sinces[i] = m.group('version')
    return sinces

def replace_file(f, replacement, commentstyle='C'):
    if commentstyle == 'Makefile':
        cbegin = r'(# @@@ GENERATED STOCK LIST BEGIN @@@\n)'
        cend = r'(# @@@ GENERATED STOCK LIST END @@@\n)'
    elif commentstyle == 'C':
        cbegin = r'(/\* @@@ GENERATED STOCK LIST BEGIN @@@ \*/\n)'
        cend = r'(/\* @@@ GENERATED STOCK LIST END @@@ \*/\n)'
    else:
        assert not 'reached'

    oldcontent = file(f).read()
    newcontent = re.sub(r'(?s)' + cbegin + r'(.*)' + cend,
                        r'\1' + replacement + r'\3',
                        oldcontent)
    # Don't waste time running diff in the trivial case
    if oldcontent == newcontent:
        return

    xgen = '%s.xgen' % f
    file(xgen, 'w').write(newcontent)
    # FIXME: status interpretation is system-dependent
    status = os.system('diff -u %s %s' % (f, xgen)) >> 8
    if status == 1:
        sys.stderr.write('%s: Updating %s\n' % (sys.argv[0], f))
        file(f, 'w').write(newcontent)
    elif status > 1:
        sys.stderr.write('%s: Diff failed for %s\n' % (sys.argv[0], f))
    os.unlink(xgen)

def update_macros(images):
    """Update header file with stock icon macro definitions."""

    # Format #defines
    hfile = base + '.h'
    defines = ['#define GWY_STOCK_%s "gwy_%s"\n' % (x.upper(), x)
               for x in sorted(images.keys())]
    p = subprocess.Popen('./utils/align-declarations.py',
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         close_fds=True)
    defines, perr = p.communicate(''.join(defines))
    replace_file(hfile, defines)

def update_documentation(images, sinces):
    """Update `documentation' in C source to list all stock icons."""

    # Format documentation entries
    # FIXME: Sometimes the image paths seem to need to `../' prepended becuase
    # content files are XIncluded from xml/ subdirectory, therefore straight
    # paths get xml/ prepended to be relative to the driver file.  Sometimes
    # this causes extra `../' in the paths.  Not sure what the `right' thing
    # is.
    cfile = base + '.c'
    template = ('/**\n'
                ' * GWY_STOCK_%s:\n'
                ' *\n'
                ' * The "%s" stock icon.\n'
                ' * <inlinegraphic fileref="gwy_%s-%d.png" format="PNG"/>\n'
                '%s'
                ' **/\n'
                '\n')
    docs = []
    for k, v in sorted(images.items()):
        words = re.sub(r'_1_1', '_1:1', k).split('_')
        for i in range(len(words)):
            # Heuristics: words without wovels are abbreviations
            if not re.search(r'[aeiouy]', words[i]):
                words[i] = words[i].upper()
            else:
                words[i] = words[i].title()
        human_name = '-'.join(words)

        if 24 in v:
            size = 24
        else:
            size = max(v)

        if sinces.has_key(k):
            s = ' *\n * Since: %s\n' % sinces[k]
            del sinces[k]
        else:
            s = ''

        docs.append(template % (k.upper(), human_name, k, size, s))
    docs = ''.join(docs)
    replace_file(cfile, docs)

def update_editor(images):
    """Update toolbox editor file with stock icon list."""

    # Format #defines
    names = ['    GWY_STOCK_%s,\n' % (x.upper()) for x in sorted(images.keys())]
    names = ''.join(names)
    replace_file(editorfile, names)

def update_svg_png_rules(built_images):
    """Update rules for generation of PNGs from SVGs."""

    rule_template = '''\
gwy_%(name)s-%(size)u.png: $(srcdir)/src/gwy_%(name)s-%(size)u.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=%(size)u --export-height=%(size)u $(INKSCAPE_EXPORT_PNGFILE)="gwy_%(name)s-%(size)u.png" "$(srcdir)/src/gwy_%(name)s-%(size)u.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_%(name)s-%(size)u.png" "gwy_%(name)s-%(size)u.crush.png"
	$(AM_V_at)if test -s "gwy_%(name)s-%(size)u.crush.png"; then mv -f "gwy_%(name)s-%(size)u.crush.png" "gwy_%(name)s-%(size)u.png"; fi
'''

    rules = []
    for name, sizes in sorted(built_images.items()):
        for size in sizes:
            d = {'name': name, 'size': size}
            rules.append(rule_template % d)

    rules = '\n'.join(rules)
    replace_file(makefile, rules, commentstyle='Makefile')

# Read everything first.  We are possibly going to update the Makefile.am.
imgs = read_images(makefile)
built_imgs = read_images(makefile, 'list-built-icons')

sincs = read_since(makefile)
update_macros(imgs)
update_documentation(imgs, sincs)
update_editor(imgs)
update_svg_png_rules(built_imgs)
# Check for unused since declarations, they are typos
if sincs:
    print 'Unused since:', sincs
    sys.exit(1)
