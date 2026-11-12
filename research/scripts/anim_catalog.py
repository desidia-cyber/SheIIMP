#!/usr/bin/env python3
"""Step 8: catalogue every AnimSequence / AnimMontage under /Game/Sparta/Characters/.

Reads the SQLite index produced by parse_dump.py and writes
  * a CSV with one row per asset (type, category, character, folder, asset name, path)
  * the category rule table in the CSV header, so the classification is auditable.

The category is a GUESS derived from the asset path only:
  1. the path is split into folder segments,
  2. each rule is a set of exact folder names; the first rule that matches any segment wins,
  3. rules are ordered by specificity (cinematic / death / hit / ... before attack / locomotion),
  4. if no folder matches, the asset *name* is tested against the same word list,
  5. otherwise the row is `other`.
The rule list below was written from the folder vocabulary measured with
scripts/anim_folders.py (i.e. from folder names that actually occur in the dump).

Usage:
  python anim_catalog.py <db.sqlite3> <out.csv>
"""
import csv
import re
import sqlite3
import sys

PREFIX = '/Game/Sparta/Characters/'

# ordered: first rule whose folder names appear in the path wins
RULES = [
    ('vo',         {'VO'}),
    ('cinematic',  {'Cinematic', 'Cinematics', 'Cutscene', 'Cutscenes', 'Intro', 'Outro',
                    'Story', 'Prologue', 'ShellMemory', 'ShellMemories'}),
    ('death',      {'Death', 'Deaths', 'Dying', 'DeathReact', 'DeathPoses'}),
    ('hit',        {'HitReactions', 'HitReaction', 'Hits', 'Hit', 'Stagger', 'Staggers',
                    'Flinch', 'Break', 'BreakReact', 'Knockdown', 'ShellKnockout'}),
    ('dodge',      {'Dodge', 'Dodges', 'Dash', 'Roll', 'Evade', 'Evasion'}),
    ('parry',      {'Parry', 'Parries', 'PerfectBlock'}),
    ('block',      {'Block', 'Blocks', 'Guard'}),
    ('riposte',    {'Riposte', 'Ripostes'}),
    ('harden',     {'Harden', 'HardenStart', 'StoneForm', 'Petrify', 'Stone'}),
    ('interact',   {'Interact', 'Interaction', 'Interactions', 'Pickup', 'Loot', 'Open',
                    'Pray', 'Kneel', 'Sit', 'Rest', 'RestPoint', 'Emote', 'Emotes',
                    'Consume', 'Drink', 'Heal', 'Revive', 'ShellRevive', 'Arise',
                    'StandUp', 'TarForge', 'Throne', 'MusicDance', 'PetsDog',
                    'DeerKilling', 'Bar'}),
    ('cast',       {'Seals', 'Seal', 'Spell', 'Spells', 'Cast', 'Casts', 'Ability',
                    'Abilities', 'Magic', 'SlayerSeal'}),
    ('ranged',     {'Crossbow', 'Ballistazooka', 'Shotgun', 'Bow', 'Sidearm', 'Sidearms',
                    'Reload', 'Shoot', 'Shooting', 'Ranged', 'Aiming'}),
    ('traversal',  {'Traversal', 'Jump', 'Jumps', 'Fall', 'Falling', 'Land', 'Landing',
                    'Climb', 'Ladder', 'Vault', 'Rope', 'Traverse'}),
    ('idle',       {'Idle', 'Idles', 'IdleBreakout', 'EquipmentIdle', 'EquipIdle', 'PreAggro'}),
    ('locomotion', {'Locomotion', 'Locomotions', 'Jog_600', 'Jog540', 'Walk', 'Walks',
                    'Strafe', 'Sprint', 'Run', 'Running', 'Turn', 'Turns', 'H2', 'H',
                    'L', 'R', 'Ls', 'RS', 'Movement'}),
    ('attack',     {'Attacks', 'Attack', 'Actions', 'Combos', 'Combo', 'Melee', 'Heavy',
                    'Light', 'Charged', 'Weapon', 'Weapons', 'Thrust', 'Slash', 'Plunge'}),
]

# words used for the name-based fallback (same order as RULES)
NAME_WORDS = [
    ('cinematic', re.compile(r'intro|outro|cinemat|cutscene', re.I)),
    ('death', re.compile(r'death|dying|_die', re.I)),
    ('hit', re.compile(r'hitreact|stagger|flinch|_hit|knockdown|_break', re.I)),
    ('dodge', re.compile(r'dodge|_dash|_roll', re.I)),
    ('parry', re.compile(r'parry|perfectblock', re.I)),
    ('block', re.compile(r'_block|_guard', re.I)),
    ('riposte', re.compile(r'riposte', re.I)),
    ('harden', re.compile(r'harden|stoneform|petrif', re.I)),
    ('interact', re.compile(r'interact|pickup|loot|_open_|pray|kneel|_sit|_rest|emote|revive|arise', re.I)),
    ('cast', re.compile(r'seal|spell|_cast', re.I)),
    ('ranged', re.compile(r'crossbow|ballista|shotgun|_bow|sidearm|reload|shoot', re.I)),
    ('traversal', re.compile(r'jump|_fall|land|climb|ladder|vault', re.I)),
    ('idle', re.compile(r'idle', re.I)),
    ('locomotion', re.compile(r'walk|jog|sprint|_run|strafe|turn', re.I)),
    ('attack', re.compile(r'attack|_atk|combo|slash|thrust|heavy|light|plunge', re.I)),
]

TYPES = ['AnimSequence', 'AnimMontage', 'AnimComposite', 'AnimStreamable',
         'BlendSpace', 'BlendSpace1D', 'AimOffsetBlendSpace', 'AnimBlueprint',
         'Skeleton', 'PhysicsAsset', 'PoseAsset']

CHAR_ROOT = {'Shells', 'Enemies', 'Bosses', 'NPCs', 'Humans', 'Player', 'Summons'}


def segments(path):
    folder = path.rsplit('/', 1)[0]
    rel = folder[len(PREFIX):] if folder.startswith(PREFIX) else folder
    return [x for x in rel.split('/') if x]


def character_of(path):
    parts = segments(path)
    if not parts:
        return ''
    if parts[0] in CHAR_ROOT:
        return parts[1] if len(parts) > 1 else parts[0]
    return parts[0]


def folder_of(path):
    return path.rsplit('/', 1)[0]


def name_of(path):
    tail = path.rsplit('/', 1)[-1]
    return tail.split('.')[-1] if '.' in tail else tail


def guess(path):
    parts = segments(path)
    segset = set(parts)
    for cat, names in RULES:
        if segset & names:
            return cat, 'folder'
    nm = name_of(path)
    for cat, rx in NAME_WORDS:
        if rx.search(nm):
            return cat, 'name'
    return 'other', 'none'


def main():
    db, out = sys.argv[1], sys.argv[2]
    con = sqlite3.connect(db)
    q = ('SELECT type, path FROM obj WHERE type IN (%s) AND path LIKE ? ORDER BY path'
         % ','.join('?' * len(TYPES)))
    rows = con.execute(q, TYPES + [PREFIX + '%']).fetchall()
    seen = set()
    recs = []
    for typ, path in rows:
        if ':' in path:
            continue
        if (typ, path) in seen:
            continue
        seen.add((typ, path))
        cat, how = guess(path)
        recs.append((typ, cat, how, character_of(path), folder_of(path), name_of(path), path))
    recs.sort(key=lambda r: (r[0], r[1], r[4], r[5]))
    with open(out, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['# category is a guess from the path; rule table below (first match wins)'])
        for cat, names in RULES:
            w.writerow(['# rule', cat, ' '.join(sorted(names))])
        w.writerow(['# after the folder rules, the asset name is tested with:',
                    ' '.join(f'{c}={r.pattern}' for c, r in NAME_WORDS)])
        w.writerow(['type', 'category', 'matched_by', 'character', 'folder', 'asset_name', 'path'])
        w.writerows(recs)
    print(f'{len(recs)} assets under {PREFIX} -> {out}')
    counts = {}
    for typ, cat, how, *_ in recs:
        counts[(typ, cat)] = counts.get((typ, cat), 0) + 1
    for (typ, cat), n in sorted(counts.items()):
        print(f'  {typ:14s} {cat:12s} {n}')
    hows = {}
    for rec in recs:
        hows[rec[2]] = hows.get(rec[2], 0) + 1
    print('  matched_by:', hows)
    seq = [r for r in recs if r[0] in ('AnimSequence', 'AnimMontage')]
    print(f'  AnimSequence+AnimMontage = {len(seq)}')


if __name__ == '__main__':
    main()
