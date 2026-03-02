#!/usr/bin/python3
import re, sys, os, pprint, itertools

lang = 'cs'
if len(sys.argv) > 1:
    lang = sys.argv[1]
olddir = os.path.join(os.environ['HOME'], 'src', 'gwyddion-2.58', 'po')
newdir = os.path.join(os.environ['HOME'], 'Projects', 'Gwyddion', 'gwyddion', 'po')

def read_po_file(filename):
    blocks = []
    block = []
    for line in open(filename):
        line = line.strip()
        block.append(line)
        if not line and block:
            blocks.append(block)
            block = []
    if block:
        blocks.append(block)

    return blocks

def is_fuzzy(block):
    for line in block:
        if line.startswith('#,'):
            if 'fuzzy' in line.split(', '):
                return True
    return False

def simple_string(block, keyword):
    for line in block:
        if line.startswith(keyword + ' '):
            text = line.split(maxsplit=1)[1]
            if len(text) > 2 and text[0] == '"' and text[-1] == '"':
                return text[1:-1]
    return None

# We only care about short labels.
def make_dict(blocks):
    d = {}
    for block in blocks:
        key = simple_string(block, 'msgid')
        value = simple_string(block, 'msgstr')
        if key and value:
            d[key] = value
    return d

# We only care about short labels.
def find_fuzziness(blocks):
    d = {}
    for block in blocks:
        key = simple_string(block, 'msgid')
        value = is_fuzzy(block)
        if key:
            d[key] = value
    return d

oldpo = read_po_file(os.path.join(olddir, lang + '.po'))
oldfuzzy = find_fuzziness(oldpo)
oldpo = make_dict(oldpo)
#pprint.pprint(oldpo)

match_old_en = re.compile(r':\Z').search
if lang == 'fr':
    match_old_lang = re.compile(r' :\Z').search
else:
    match_old_lang = match_old_en

new_to_old_en = lambda x: x + ':'
old_to_new_lang = lambda x: x.rstrip(':').rstrip()

newpo = read_po_file(os.path.join(newdir, lang + '.po'))
for j, block in enumerate(newpo):
    if not is_fuzzy(block):
        continue

    key = simple_string(block, 'msgid')
    if not key or match_old_en(key):
        continue

    oldkey = new_to_old_en(key)
    #print(key, oldkey, oldkey in oldpo)
    if oldkey in oldpo and match_old_lang(oldpo[oldkey]):
        for i, line in enumerate(block):
            if line.startswith('#, '):
                if not oldfuzzy[oldkey]:
                    block[i] = line.replace(', fuzzy', '')
            if line.startswith('msgstr '):
                fixed_val = old_to_new_lang(oldpo[oldkey])
                block[i] = 'msgstr "%s"' % fixed_val

        newpo[j] = [line for line in block if line != '#']

open(os.path.join(newdir, lang + '-new.po'), 'w').write('\n'.join(itertools.chain(*newpo)) + '\n')
