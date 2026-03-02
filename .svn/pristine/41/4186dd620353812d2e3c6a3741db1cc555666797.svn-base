#!/usr/bin/python2
# Fix odd extra spaces in the table of contents if the header has an icon.
# Fixing only TeX files or only output files does not work.  Must fix all, for
# whatever reason.
import sys, os, re

tex_file = sys.argv[1]

fixsect1re = re.compile('(?P<command>\\\\(?:sub)*section)\s*{\s+')
fixsect2re = re.compile('(?P<command>\\\\(?:sub)*section)\[\{\s+(?P<title>[^]]+?)\s*\}\]{\s*')
fixtocre = re.compile('(?P<command>\\\\contentsline\s*\{(?:sub)*section\})\s*\{\s+(?P<title>[^}]+?)\s*\}')

fh = open(tex_file, 'r+')
tex = fh.read()
newtex = fixsect1re.sub(r'\g<command>{', tex)
newtex = fixsect2re.sub(r'\g<command>[{\g<title>}]{', newtex)
# This is some Python repr() breaking things?
newtex = re.sub(r'b\'<:\'(.*?)b\':>\'', r'<:\1:>', newtex)

if newtex != tex:
    fh.seek(0)
    fh.truncate(0)
    fh.write(newtex)

toc_file = re.sub(r'\.tex$', r'.toc', tex_file)
fh = open(toc_file, 'r+')
toc = fh.read()
newtoc = fixtocre.sub(r'\g<command>{\g<title>}', toc)

if newtoc != toc:
    fh.seek(0)
    fh.truncate(0)
    fh.write(newtoc)

aux_file = re.sub(r'\.tex$', r'.aux', tex_file)
fh = open(aux_file, 'r+')
aux = fh.read()
newaux = fixtocre.sub(r'\g<command>{\g<title>}', aux)

if newaux != aux:
    fh.seek(0)
    fh.truncate(0)
    fh.write(newaux)

sys.exit(0)

