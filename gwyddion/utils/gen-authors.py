#!/usr/bin/python2
# vim: set fileencoding=utf-8 :
# $Id: gen-authors.py 22375 2019-08-07 08:26:37Z yeti-dn $
import re, sys, locale
from xml.sax.saxutils import escape

if len(sys.argv) != 3 or sys.argv[2] not in ('web', 'header'):
    print 'gen-authors.py AUTHORS {web|header}'
    sys.exit(1)

filename = sys.argv[1]
mode = sys.argv[2]

cyrillic_translit = u'''\
АAБBВVГGДDЕEЁEЖZЗZИIЙJКKЛLМMНNОOПPРRСSТTУUФFХHЦCЧCШSЩSЪZЫZЬZЭEЮUЯA\
аaбbвvгgдdеeёeжzзzиiйjкkлlмmнnоoпpрrсsтtуuфfхhцcчcшsщsъzыzьzэeюuяa\
'''
cyrillic_map = dict((cyrillic_translit[2*i], cyrillic_translit[2*i + 1])
                    for i in range(len(cyrillic_translit)/2))

contrib_re = re.compile(ur'(?ms)^(?P<name>\S[^<>]+?)'
                        ur'(\s+<(?P<email>[^<> ]+)>)?\n'
                        ur'(?P<what>(?:^ +\S[^\n]+\n)+)')

def sortkey(x):
    last = [y for y in x.split() if not y.startswith(u'(')][-1]
    x = last + u' ' + x
    if x[0] in cyrillic_map:
        x = u''.join(cyrillic_map.get(c, c) for c in x)
    return locale.strxfrm(x.encode('utf-8'))

def make_sectid(section):
    return re.sub(ur'[^a-zA-Z]', '', section.lower())

def parse_contributors(text, section):
    header_re = re.compile(ur'(?ms)^=== ' + section + ur' ===(?P<body>.*?)^$')
    text = header_re.search(text).group('body')
    contributors = {}
    names = []
    for m in contrib_re.finditer(text):
        name, email = m.group('name'), m.group('email')
        what = re.sub(ur'(?s)\s+', ur' ', m.group('what').strip())
        contributors[name] = (email, what)
        names.append(name)
    return contributors, names

def format_html_list(text, section):
    contributors, names = parse_contributors(text, section)
    sectid = make_sectid(section)
    out = [u'<p id="%s"><b>%s:</b></p>' % (sectid, section)]
    out.append(u'<dl>')
    for name in sorted(names, key=sortkey):
        email, what = contributors[name]
        out.append(u'<dt>%s, <code>%s</code></dt>' % (name, email))
        out.append(u'<dd>%s</dd>' % what)
    out.append(u'</dl>')
    return u'\n'.join(out).encode('utf-8')

def format_header_array(text, section):
    contributors, names = parse_contributors(text, section)
    sectid = make_sectid(section)
    out = [u'static const gchar %s[] =' % sectid]
    out.extend([u'  "%s\\n"' % name for name in names])
    out.append(u';')
    return u'\n'.join(out).encode('utf-8')

text = file(filename).read().decode('utf-8')

if mode == 'web':
    # Can't set a UTF-8 locale on MS Windows so avoid it in the header mode.
    locale.setlocale(locale.LC_ALL, 'en_US.UTF-8')
    print format_html_list(text, 'Developers')
    print format_html_list(text, 'Translators')

if mode == 'header':
    print '/* This is a %s file */' % 'GENERATED'
    print format_header_array(text, 'Developers')
    print format_header_array(text, 'Translators')
