#!/usr/bin/python2
# vim: set ts=4 sw=4 et fileencoding=utf-8 :
#
# Recursively resolve XIncludes in a XML file.
# Limitations:
# - Only xi namespace is recognized, i.e. <xi:include .../>
# - includes are expanded also in CDATA sections
# - many others
# But it can resolve the includes more than 100× faster than xsltproc.
#
# direct xsltproc:
# real	1m7.962s
# user	1m5.179s
# sys	0m1.744s
#
# xmlunfold:
# real	0m0.203s
# user	0m0.108s
# sys	0m0.066s
#
# xsltproc on unfolded result:
# real	0m27.518s
# user	0m26.565s
# sys	0m0.345s

import sys, re, os

if len(sys.argv) < 2:
    sys.stdout.write('%s: MAIN-DOCUMENT.xml >SINGLE-FILE.xml\n' % sys.argv[0])
    sys.stdout.write('Resolve XIncludes, producing a single XML file.\n')
    sys.exit(0)

searchpath = []
filename = os.path.abspath(sys.argv[1])
inputdir = os.path.dirname(filename)
searchpath.append(inputdir)
searchpath.append(os.path.abspath(os.path.join(inputdir, '..', 'formulas')))
searchpath.append(os.path.abspath(os.path.join(inputdir, '..', '..', 'common', 'formulas')))
imagepath = os.path.abspath(os.path.join(inputdir, '..', '..', 'common', 'srcimages'))

subxinclude = re.compile(r'''(?s)<xi:include\s+href=(['"])(?P<file>[^'"]+)\1\s*/>''').sub
subcruft = re.compile(r'(?s)<\?xml\s[^>]*\?>\s*'
                      r'|<!DOCTYPE\s[^>]*>\s*'
                      r'|<!--\s.*?-->').sub

subgwyicon = re.compile(r'''(?s)<gwy:icon\s+name=(['"])(?P<name>[^'"]+)\1\s*/>''').sub

gwyicon_png = '''\
<guiicon>
  <inlinemediaobject>
    <imageobject>
      <imagedata fileref='gwy_%(name)s-24.png' format='PNG'/>
    </imageobject>
  </inlinemediaobject>
</guiicon>'''

gwyicon_svg = '''\
<guiicon>
  <inlinemediaobject>
    <imageobject>
      <imagedata fileref='gwy_%(name)s-24.pdf' format='PDF'/>
    </imageobject>
    <imageobject>
      <imagedata fileref='gwy_%(name)s-24.png' format='PNG'/>
    </imageobject>
  </inlinemediaobject>
</guiicon>'''

currentfile = [filename]

def xinclude(match):
    global currentfile
    filename = match.group('file')
    for p in searchpath:
        try:
            fnm = os.path.join(p, filename)
            text = file(fnm).read()
            currentfile.append(fnm)
            text = subxinclude(xinclude, subcruft('', text).strip())
            del currentfile[-1]
            return text
        except IOError:
            pass
    lineno = match.string.count('\n', 0, match.start()) + 1
    sys.stderr.write('%s:%d: unresolved xi:include of ‘%s’\n'
                     % (currentfile[-1], lineno, filename))
    return ''

def gwyicon(match):
    name = match.group('name')
    d = {'name': name}
    fnm = os.path.join(imagepath, 'gwy_%(name)s-24' % d)
    if os.path.exists(fnm + '.svg'):
        return gwyicon_svg % d
    sys.stderr.write('Cannot find %s.svg\n' % fnm)
    if os.path.exists(fnm + '.png'):
        return gwyicon_png % d
    lineno = match.string.count('\n', 0, match.start()) + 1
    sys.stderr.write('%s:%d: unresolved gwy:icon of ‘%s’\n'
                     % (currentfile[-1], lineno, name))
    return ''

try:
    text = file(filename).read()
except IOError:
    sys.stderr.write('Cannot read ‘%s’\n' % filename)
    sys.exit(1)

text = subxinclude(xinclude, text)
text = subgwyicon(gwyicon, text)
sys.stdout.write(text)
