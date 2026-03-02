#!/usr/bin/python2
# Written by Yeti <yeti@gwyddion.net>. Public domain.
import sys, re

debug = False

if len(sys.argv) < 3:
    print sys.argv[0], 'SECTION_FILE [--standard-files=FILE,...]'
    sys.exit(1)

title_re = re.compile(r'<TITLE>(?P<object>\w+)</TITLE>')
file_re = re.compile(r'<FILE>(?P<file>\w+)</FILE>')
section_file = sys.argv[1]
ignore_files = {}
for x in sys.argv[3:]:
    if x.startswith('--standard-files='):
        for f in x[len('--standard-files='):].split(','):
            ignore_files[f] = 1
    else:
        sys.stderr.write(sys.argv[0] + ': Unknown option ' + x + '\n')

fh = file(section_file, 'r')
lines = [line.strip() for line in fh.readlines()]
fh.close()
lines = [line for line in lines if line]

def add_std(tostd, ucname):
    tostd.add('GWY_IS_' + ucname)
    tostd.add('GWY_IS_' + ucname + '_CLASS')
    tostd.add('GWY_' + ucname)
    tostd.add('GWY_' + ucname + '_CLASS')
    tostd.add('GWY_' + ucname + '_GET_CLASS')
    tostd.add('GWY_TYPE_' + ucname)

sections = []
section = None
for line in lines:
    if line == '<SECTION>':
        assert section is None
        section = {'Symbols': []}
        continue

    if line == '</SECTION>':
        assert section is not None
        if section['FILE'] not in ignore_files and 'Standard' in section:
            tostd = set()
            substd = section['Standard']
            for symbol in substd:
                m = re.search(r'^GWY_(\w+)_GET_CLASS$', symbol)
                if m:
                    add_std(tostd, m.group(1))
                m = re.search(r'^GWY_TYPE_(\w+)$', symbol)
                if m:
                    add_std(tostd, m.group(1))
                m = re.search(r'^gwy_\w+_get_type$', symbol)
                if m:
                    tostd.add(symbol)

            if debug:
                print section['FILE'], tostd
            section['Symbols'].extend(symbol for symbol in substd
                                      if symbol not in tostd)
            section['Standard'] = [symbol for symbol in substd
                                   if symbol in tostd]
        section = None
        continue

    m = re.search(r'^<FILE>([^<]+)</FILE>', line)
    if m:
        assert section is not None
        section['FILE'] = m.group(1)
        sections.append(section)
        is_standard = False
        continue

    m = re.search(r'^<TITLE>([^<]+)</TITLE>', line)
    if m:
        assert section is not None
        section['TITLE'] = m.group(1)
        continue

    if line == '<SUBSECTION Standard>':
        is_standard = True
        section['Standard'] = []
        continue

    assert line[0] != '<'
    if is_standard:
        section['Standard'].append(line)
    else:
        section['Symbols'].append(line)

fh = file(section_file, 'w')
for section in sections:
    fh.write('<SECTION>\n')
    fh.write('<FILE>%s</FILE>\n' % (section['FILE']))
    if 'TITLE' in section:
        fh.write('<TITLE>%s</TITLE>\n' % (section['TITLE']))
    fh.write('\n'.join(section['Symbols']) + '\n')
    if 'Standard' in section:
        fh.write('<SUBSECTION Standard>\n')
        fh.write('\n'.join(section['Standard']) + '\n')
    fh.write('</SECTION>\n\n')
fh.close()
