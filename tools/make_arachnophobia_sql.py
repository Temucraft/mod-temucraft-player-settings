#!/usr/bin/env python3
"""Builds data/sql/db-world/base/mod_temucraft_arachnophobia.sql: which creature display a player with Arachnophobia
Mode on sees instead of each spider-family display (mod-temucraft-player-settings).

    python3 tools/make_arachnophobia_sql.py <CreatureDisplayInfo.dbc> <CreatureModelData.dbc> [out.sql]

Use the server's DBC copies (they are the client's patch-W ones). Run it again after new displays are added.

How a display is picked:
  1. The source display's model path (CreatureModelData.ModelName) puts it in a group (see GROUPS): spiders -> crabs,
     giant spiders and tarantulas -> big (deep sea / "sand") crabs, mechanical spiders -> hermit crab, nerubians and
     crypt fiends / crypt lords / dryders -> makrura, flying nerubians -> wasps, spider mounts -> crab mount.
  2. Among the group's target displays, the one with the closest colour (from the texture names) and size wins.
  3. `scale` = source size / target size, where size = display scale x model scale x cube root of the model's
     bounding box. The server multiplies the object scale it sends by it, so the crab is as big as the spider was.
     (Display only: the server keeps the creature's real size, reach and hit box.)
"""
import math, re, struct, sys

def read_dbc(path):
    data = open(path, 'rb').read()
    magic, count, fields, size, strsize = struct.unpack('<4s4I', data[:20])
    if magic != b'WDBC':
        raise SystemExit(path + ': not a DBC')
    strings = data[20 + count * size:]
    def string(offset):
        return strings[offset:strings.index(b'\0', offset)].decode('latin-1')
    rows = []
    for i in range(count):
        raw = data[20 + i * size:20 + (i + 1) * size]
        rows.append((struct.unpack('<%dI' % fields, raw), struct.unpack('<%df' % fields, raw)))
    return rows, string

# Model folder (Creature\<folder>\) or file name -> group. File names win over folders (GnomeSpidertank holds robots).
SOURCE_FILES = {
    'gnomespidertank.mdx': 'hermitcrab', 'gnomespidertank02.mdx': 'hermitcrab',
    'minespiderboss.mdx': 'sandcrab',
}
SOURCE_FOLDERS = {
    # spiders -> crabs
    'spider': 'crab', 'minespider': 'crab', 'minespider2': 'crab', 'crystalspider': 'crab', 'lavaspider': 'crab',
    'shalespider': 'crab', 'bonespider': 'crab', 'bonespider2': 'crab', 'firespider': 'crab',
    'spiderdemon': 'crab', 'spiderdemon_spider': 'crab', 'nerubianspiderling': 'crab', 'undeadnerubianbeast': 'crab',
    # giant spiders, tarantulas -> sand crabs (big crabs)
    'giantspider': 'sandcrab', 'giantspider2': 'sandcrab', 'tarantula': 'sandcrab', 'tarantula2': 'sandcrab',
    'firespiderboss': 'sandcrab', 'manaspiderboss': 'sandcrab', 'undercrawler': 'sandcrab',
    # mechanical spiders -> hermit crab
    'mechanicalspider': 'hermitcrab', 'mechanicalspiderpet': 'hermitcrab', 'babyspidertank': 'hermitcrab',
    'mechagonspidertank': 'hermitcrab',
    # nerubians, crypt fiends, crypt lords, dryders -> makrura
    'nerubian': 'makrura', 'nerubianwarrior': 'makrura', 'nerubiancaster': 'makrura', 'nerubianpriest': 'makrura',
    'cryptfiend': 'makrura', 'cryptlord': 'makrura', 'dryder': 'makrura', 'aranasi': 'makrura',
    # flying nerubians -> wasps (3.3.5 has no skitterfly)
    'flyingnerubian': 'flyer',
    # spider mounts -> crab mount
    'spidermount': 'mount',
}
# Target displays: model file -> group. The first ones are the stock 3.3.5 / Cataclysm models.
TARGET_FILES = {
    'crab.mdx': 'crab', 'crab2.mdx': 'crab',
    'deepseacrab.m2': 'sandcrab',
    'hermitcrab.mdx': 'hermitcrab',
    'lobstrok.mdx': 'makrura', 'lobstrokoutland.mdx': 'makrura', 'makrura2.mdx': 'makrura',
    'silithidwasp.mdx': 'flyer', 'giantwasp.mdx': 'flyer',
    'crabmount.mdx': 'mount',
}
# A group may also take another group's targets, at a cost: a very big spider becomes a big (deep sea) crab rather
# than a small crab blown up five times.
EXTRA_TARGETS = {'crab': [('sandcrab', 0.25)], 'sandcrab': [('crab', 0.25)]}
GROUP_NAMES = {'crab': 'spider -> crab', 'sandcrab': 'giant spider -> sand crab', 'hermitcrab': 'mechanical spider -> hermit crab',
               'makrura': 'nerubian -> makrura', 'flyer': 'flying nerubian -> wasp', 'mount': 'spider mount -> crab mount'}

COLOURS = [  # texture word -> colour
    ('albino', 'white'), ('ivory', 'white'), ('white', 'white'), ('frost', 'white'), ('silver', 'grey'),
    ('steel', 'grey'), ('grey', 'grey'), ('gray', 'grey'), ('slate', 'grey'), ('undead', 'grey'), ('scourge', 'grey'),
    ('black', 'black'), ('dark', 'black'), ('diamond', 'black'),
    ('vermillian', 'red'), ('blood', 'red'), ('red', 'red'), ('magma', 'red'), ('lava', 'red'),
    ('orange', 'orange'), ('bronze', 'orange'), ('ochre', 'yellow'), ('yellow', 'yellow'), ('tan', 'brown'),
    ('gold', 'yellow'), ('brown', 'brown'), ('cave', 'brown'),
    ('green', 'green'), ('olive', 'green'), ('jungle', 'green'), ('wetlands', 'green'), ('turquoise', 'blue'),
    ('saphire', 'blue'), ('sapphire', 'blue'), ('blue', 'blue'), ('crystal', 'blue'), ('biolucent', 'blue'),
    ('purple', 'purple'), ('violet', 'purple'),
]
NEAR = {('white', 'grey'), ('grey', 'black'), ('red', 'orange'), ('orange', 'yellow'), ('orange', 'brown'),
        ('yellow', 'brown'), ('blue', 'purple'), ('green', 'blue'), ('red', 'purple'), ('brown', 'black')}

def colour(texture):
    t = texture.lower()
    for word, name in COLOURS:
        if word in t:
            return name
    return None

def colour_cost(a, b):
    if a is None or b is None or a == b:
        return 0.0 if a == b else 0.6
    return 0.4 if (a, b) in NEAR or (b, a) in NEAR else 1.0

def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    models, mstring = read_dbc(sys.argv[2])
    model = {}
    for v, f in models:
        path = mstring(v[2]).replace('/', '\\')
        parts = path.lower().split('\\')
        box = [f[20] - f[17], f[21] - f[18], f[22] - f[19]]
        extent = (box[0] * box[1] * box[2]) ** (1 / 3) if min(box) > 0.01 else 0.0
        model[v[0]] = {'path': path, 'folder': parts[1] if len(parts) > 2 and parts[0] == 'creature' else '',
                       'file': parts[-1], 'scale': f[4] if f[4] > 0 else 1.0, 'extent': extent}
    # models without a bounding box take the size of another model of their group (or a mine spider)
    by_folder = {}
    for m in model.values():
        if m['extent'] > 0:
            by_folder.setdefault(m['folder'], m['extent'])
    for m in model.values():
        if m['extent'] <= 0:
            m['extent'] = by_folder.get(m['folder']) or by_folder.get('minespider', 3.8)

    displays, dstring = read_dbc(sys.argv[1])
    sources, targets = [], {}
    for v, f in displays:
        m = model.get(v[1])
        if not m:
            continue
        texture = dstring(v[6]) if v[6] < 1 << 31 else ''
        entry = {'id': v[0], 'model': m, 'size': f[4] * m['scale'] * m['extent'], 'colour': colour(texture),
                 'texture': texture}
        if m['file'] in TARGET_FILES:
            targets.setdefault(TARGET_FILES[m['file']], []).append(entry)
        group = SOURCE_FILES.get(m['file'])
        if group is None and m['file'] not in ('gnomebot.mdx', 'gnomealertbot.mdx', 'gnomebombot.mdx', 'gnomepounder.mdx',
                                               'gnomepoundervehicle.mdx'):
            group = SOURCE_FOLDERS.get(m['folder'])
        if group and f[4] > 0:
            sources.append((group, entry))

    rows, counts = [], {}
    for group, src in sources:
        best = None
        pool = [(dst, 0.0) for dst in targets[group]]
        for other, extra in EXTRA_TARGETS.get(group, []):
            pool += [(dst, extra) for dst in targets[other]]
        for dst, extra in pool:
            if dst['size'] <= 0:
                continue
            ratio = src['size'] / dst['size']
            cost = extra + colour_cost(src['colour'], dst['colour']) + 0.5 * abs(math.log(ratio))
            if dst['model']['file'] in ('crab2.mdx', 'makrura2.mdx', 'giantwasp.mdx'):
                cost += 0.3   # retail imports: only when the colour fits clearly better
            if best is None or cost < best[0] - 1e-9 or (abs(cost - best[0]) < 1e-9 and dst['id'] < best[1]['id']):
                best = (cost, dst, ratio)
        if not best:
            continue
        _, dst, ratio = best
        scale = min(max(ratio, 0.05), 20.0)
        counts[group] = counts.get(group, 0) + 1
        comment = '%s %s (%s) -> %s %s' % (src['model']['file'], src['texture'] or '-', GROUP_NAMES[group],
                                           dst['model']['file'], dst['texture'] or '-')
        rows.append((src['id'], dst['id'], scale, group, comment.replace("'", '')))
    rows.sort()

    out = open(sys.argv[3], 'w', newline='\n') if len(sys.argv) > 3 else sys.stdout
    out.write('-- Arachnophobia Mode (mod-temucraft-player-settings): what a player with the setting on sees instead of\n')
    out.write('-- each spider-family creature display. Made by tools/make_arachnophobia_sql.py from the server DBCs;\n')
    out.write('-- run the tool again instead of editing by hand. scale: the object scale sent to that player is\n')
    out.write('-- multiplied by it so the new model is as big as the old one (display only).\n')
    out.write('-- %s\n' % ', '.join('%s %d' % (GROUP_NAMES[g], counts[g]) for g in GROUP_NAMES if g in counts))
    out.write('DROP TABLE IF EXISTS `mod_temucraft_arachnophobia`;\n')
    out.write('CREATE TABLE `mod_temucraft_arachnophobia` (\n'
              '  `display_from` INT UNSIGNED NOT NULL,\n'
              '  `display_to` INT UNSIGNED NOT NULL,\n'
              '  `scale` FLOAT NOT NULL DEFAULT 1,\n'
              "  `kind` VARCHAR(16) NOT NULL DEFAULT '',\n"
              "  `comment` VARCHAR(160) NOT NULL DEFAULT '',\n"
              '  PRIMARY KEY (`display_from`)\n'
              ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;\n\n')
    for i in range(0, len(rows), 200):
        out.write('INSERT INTO `mod_temucraft_arachnophobia` (`display_from`, `display_to`, `scale`, `kind`, `comment`) VALUES\n')
        chunk = rows[i:i + 200]
        out.write(',\n'.join("(%d, %d, %.3f, '%s', '%s')" % r for r in chunk) + ';\n\n')
    print('rows: %d  %s' % (len(rows), counts), file=sys.stderr)

if __name__ == '__main__':
    main()
