#!/usr/bin/python2
import sys, re, os

def fix_file(filename):
    origtext = file(filename).read()
    basename = os.path.basename(filename)
    basename = re.sub(r'\.xml$', '', basename)
    text = re.sub(r'''(?P<prefix><(?:informalequation|inlineequation) id=["'])[-_A-Za-z0-9]+(?P<suffix>["']>)''',
                  r'\g<prefix>' + basename + r'\g<suffix>',
                  origtext)
    text = re.sub(r'''(?P<prefix><imagedata fileref=["'])[-_A-Za-z0-9]+(?P<suffix>\.svg["'] format=["']SVG["']/>)''',
                  r'\g<prefix>' + basename + r'\g<suffix>',
                  text)
    if text != origtext:
        print 'Fixing', filename
        file(filename, 'w').write(text)

for filename in sys.argv[1:]:
    fix_file(filename)
