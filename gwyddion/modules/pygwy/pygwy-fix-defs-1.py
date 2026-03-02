#!/usr/bin/python2
# $Id: pygwy-fix-defs-1.py 21705 2018-11-29 09:17:12Z yeti-dn $
# perform simple text-transform fixes on the defs file
import sys, re

fixes = [
    (r'\(define-object Serializable', r'(define-interface Serializable'),
    (r'\bGWY_TYPE_SELECTION_GRAPH1_DAREA\b', r'GWY_TYPE_SELECTION_GRAPH_1DAREA'),
    (r'\bGWY_TYPE_NL_FIT_PRESET\b', r'GWY_TYPE_NLFIT_PRESET'),
    (r'\bGWY_TYPE_CD_LINE\b', r'GWY_TYPE_CDLINE'),
    (r'\bGwyXyz\b', r'GwyXYZ'),
    (r'\bGwyXy\b', r'GwyXY'),
    (r'\bGwyRgba\b', r'GwyRGBA'),
    (r'\bGwySiUnit\b', r'GwySIUnit'),
    (r'\bGwySiValueFormat\b', r'GwySIValueFormat'),
    (r'\bGwyCdLine\b', r'GwyCDLine'),
    (r'\bGwyGl([A-Z])', r'GwyGL\1'),
    (r'\bGwy([HV])ruler\b', r'Gwy\1Ruler'),
    (r'\bGwy([HV])marker', r'Gwy\1Marker'),
    (r'\bGwyNLFitter\b', r'GwyMathNLFit'),
    (r'\bGwyMathNlfit\b', r'GwyMathNLFit'),
    (r'\(define-method ([1-3]d)([a-z]+)', r'(define-method \2\1'),
    (r'"(as|assert|def|exec|from|global|import|in|is|lambda|pass|print|raise|with|yield)"\)', r'"\1_")'),
]

text = file(sys.argv[1]).read()
for fix in fixes:
    text, nsub = re.subn(fix[0], fix[1], text)
    if nsub:
        continue
    sys.stderr.write('Fix-regexp %s did not match anything.\n' % repr(fix[0]))
sys.stdout.write(text)
