#!/usr/bin/python2
import os, sys, re, subprocess, tempfile
from optparse import OptionParser

parser = OptionParser(usage='''\
xhtml-highlight.py --highlight-program=PROGRAM [FILE...]
Performs source code highlighting on given files.  Files are overwritten.''')

parser.add_option('-p', '--highlight-program', dest='highlight_program',
                  type='string', default=':', metavar='PROGRAM',
                  help='Highlighting program.  Should be source-highlight or :.')
parser.add_option('-l', '--language', dest='src_lang',
                  type='string', default='python', metavar='LANG',
                  help='Source code language.')

(options, args) = parser.parse_args()

out_format = 'xhtml-css'

if options.highlight_program == ':':
    sys.stderr.write('No highlighting program available, source code will not be highlighted.\n')
    sys.exit(0)

command = [
    options.highlight_program,
    '--src-lang=' + options.src_lang,
    '--out-format=' + out_format
]

program_listing_re = (r'(?ms)'
                      r'(?P<head><pre\s+class=["\']programlisting["\']>)'
                      r'(?P<code>.*?)'
                      r'(?P<tail></pre>)')

def highlight_code(m):
    code = m.group('code')
    tmp = tempfile.TemporaryFile()
    tmp.write(code)
    tmp.flush()
    tmp.seek(0)
    try:
        hlcode = subprocess.check_output(command, stdin=tmp)
    except OSError, IOError:
        sys.stderr.write('Highlighting program execution failed, preserving code as-is.\n')
        return code

    hlcode = re.sub(r'(?ms)\A<!--.*?-->\s*', r'', hlcode)
    hlcode = re.sub(r'\A<pre><tt>', r'', hlcode)
    hlcode = re.sub(r'</tt></pre>\s*\Z', r'', hlcode)
    return m.group('head') + hlcode + m.group('tail')

for filename in args:
    if not os.path.exists(filename):
        sys.stderr.write('Cannot find %s, skipping.\n' % filename)
        continue

    f = file(filename)
    text = f.read()
    f.close()

    if re.match(r'<span class=["\'](normal|keyword|symbol|function|type|string|literal)["\']', text):
        sys.stderr.write('File %s was already highlighted, skipping it.\n'
                         % filename)

    text = re.sub(program_listing_re, highlight_code, text)
    file(filename, 'w').write(text)
