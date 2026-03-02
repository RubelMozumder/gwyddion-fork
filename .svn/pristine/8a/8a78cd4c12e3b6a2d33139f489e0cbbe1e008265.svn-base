#!/usr/bin/python2
import sys, re

filename = sys.argv[1]
factor = float(sys.argv[2])
fh = open(filename)
svg = fh.read()
fh.close()

def multiply(m):
    return m.group('pre') + '%.8g' % (factor*float(m.group('value'))) + m.group('post')

# Scale width and height attributes.
svg = re.sub(r'(?s)(?P<pre><svg\b[^>]+\bwidth=["\'])(?P<value>[0-9.]+)(?P<post>[a-z]*["\'])', multiply, svg)
svg = re.sub(r'(?s)(?P<pre><svg\b[^>]+\bheight=["\'])(?P<value>[0-9.]+)(?P<post>[a-z]*["\'])', multiply, svg)

# Scale viewBox.
svg = re.sub(r'(?s)(?P<pre><svg\b[^>]+\bviewBox=["\'][0-9.]+\s+[0-9.]+\s+)(?P<value>[0-9.]+)(?P<post>\s+[0-9.]+["\'])', multiply, svg)
svg = re.sub(r'(?s)(?P<pre><svg\b[^>]+\bviewBox=["\'][0-9.]+\s+[0-9.]+\s+[0-9.]+\s+)(?P<value>[0-9.]+)(?P<post>["\'])', multiply, svg)

# Add scale transform to the first group after </defs>.  Simplistic, but works for pdf2svg output.
# XXX: No longer works in F36.
# svg = re.sub(r'(?s)(?P<pre></defs>\s*<g\b)', r'\g<pre> transform="scale(' + '%.8g' % factor + r')"', svg)
# Add scale transform around everything after </defs> until the end of <svg>.
svg = re.sub(r'(?s)(?P<pre></defs>\s*)(?P<content>.*?)(?P<post>\s*</svg>)',
             r'\g<pre><g transform="scale(' + '%.8g' % factor + r')">\g<content></g>\g<post>', svg)

fh = open(filename, 'w')
fh.write(svg)
fh.close()

