#!/usr/bin/env python3
"""
fh_scope.py - scan YOUR OWN copy of the game's RPG Maker MV data folder
and report which engine features the 3DS port must implement.

It prints counts and identifiers only (command codes, plugin command
names, formula shapes) - no dialogue text - so the report is safe to
paste back into chat.

Usage:
    python fh_scope.py "C:/path/to/game/www/data"  > report.txt
"""
import json, os, re, sys
from collections import Counter

CODE_NAMES = {
    0:"End", 101:"Show Text", 401:"Text line", 102:"Show Choices", 402:"When [choice]",
    403:"When Cancel", 404:"End Choices", 103:"Input Number", 104:"Select Item",
    105:"Scrolling Text", 405:"Scrolling line", 108:"Comment", 408:"Comment line",
    111:"Conditional Branch", 411:"Else", 412:"End Branch", 112:"Loop", 413:"Repeat",
    113:"Break Loop", 115:"Exit Event", 117:"Common Event", 118:"Label", 119:"Jump to Label",
    121:"Control Switches", 122:"Control Variables", 123:"Self Switch", 124:"Timer",
    125:"Change Gold", 126:"Change Items", 127:"Change Weapons", 128:"Change Armors",
    129:"Change Party", 132:"Battle BGM", 133:"Victory ME", 134:"Save Access",
    135:"Menu Access", 136:"Encounter Enable", 137:"Formation Access", 138:"Window Color",
    139:"Defeat ME", 201:"Transfer Player", 202:"Vehicle Location", 203:"Set Event Location",
    204:"Scroll Map", 205:"Move Route", 505:"Move Route step", 206:"Vehicle On/Off",
    211:"Transparency", 212:"Show Animation", 213:"Balloon Icon", 214:"Erase Event",
    216:"Followers", 217:"Gather Followers", 221:"Fadeout", 222:"Fadein", 223:"Tint Screen",
    224:"Flash Screen", 225:"Shake Screen", 230:"Wait", 231:"Show Picture", 232:"Move Picture",
    233:"Rotate Picture", 234:"Tint Picture", 235:"Erase Picture", 236:"Weather",
    241:"Play BGM", 242:"Fadeout BGM", 243:"Save BGM", 244:"Resume BGM", 245:"Play BGS",
    246:"Fadeout BGS", 249:"Play ME", 250:"Play SE", 251:"Stop SE", 261:"Play Movie",
    281:"Map Name Display", 282:"Change Tileset", 283:"Change Battleback", 284:"Change Parallax",
    285:"Get Location Info", 301:"Battle Processing", 601:"If Win", 602:"If Escape",
    603:"If Lose", 604:"End Battle Branch", 302:"Shop", 605:"Shop item", 303:"Name Input",
    311:"Change HP", 312:"Change MP", 326:"Change TP", 313:"Change State", 314:"Recover All",
    315:"Change EXP", 316:"Change Level", 317:"Change Parameter", 318:"Change Skill",
    319:"Change Equipment", 320:"Change Name", 321:"Change Class", 322:"Change Actor Image",
    324:"Change Nickname", 325:"Change Profile", 331:"Enemy HP", 332:"Enemy MP", 342:"Enemy TP",
    333:"Enemy State", 334:"Enemy Recover All", 335:"Enemy Appear", 336:"Enemy Transform",
    337:"Battle Animation", 339:"Force Action", 340:"Abort Battle", 351:"Open Menu",
    352:"Open Save", 353:"Game Over", 354:"Return to Title", 355:"Script", 655:"Script line",
    356:"Plugin Command",
}

codes = Counter()           # command code -> uses
where = {}                  # command code -> first place seen
plugin_cmds = Counter()     # first word of plugin command -> uses
script_calls = 0
script_samples = []
move_codes = Counter()      # move route sub-commands

def walk(cmds, label):
    global script_calls
    for c in cmds or []:
        code = c.get("code", -1)
        codes[code] += 1
        where.setdefault(code, label)
        p = c.get("parameters", [])
        if code == 356 and p:
            plugin_cmds[str(p[0]).split(" ")[0]] += 1
        elif code == 355:
            script_calls += 1
            if len(script_samples) < 25:
                script_samples.append(f"{label}: {str(p[0])[:70]}")
        elif code in (205,) and len(p) > 1 and isinstance(p[1], dict):
            for m in p[1].get("list", []):
                move_codes[m.get("code", -1)] += 1

def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)

def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    d = sys.argv[1]
    files = sorted(os.listdir(d))

    maps = [f for f in files if re.fullmatch(r"Map\d{3}\.json", f)]
    total_events = total_pages = 0
    for m in maps:
        data = load(os.path.join(d, m))
        for ev in data.get("events") or []:
            if not ev: continue
            total_events += 1
            for i, page in enumerate(ev.get("pages", [])):
                total_pages += 1
                walk(page.get("list"), f"{m} ev{ev['id']} p{i}")

    common = load(os.path.join(d, "CommonEvents.json"))
    for ce in common:
        if ce: walk(ce.get("list"), f"Common {ce['id']}")

    troops = load(os.path.join(d, "Troops.json"))
    for t in troops:
        if not t: continue
        for i, page in enumerate(t.get("pages", [])):
            walk(page.get("list"), f"Troop {t['id']} p{i}")

    def count(name):
        try: return sum(1 for x in load(os.path.join(d, name)) if x and x.get("name"))
        except FileNotFoundError: return 0

    print("=== DATABASE SIZE ===")
    for n in ["Actors","Classes","Skills","Items","Weapons","Armors",
              "Enemies","Troops","States","Animations","Tilesets"]:
        print(f"{n:12s} {count(n + '.json')}")
    print(f"Maps         {len(maps)}  (events {total_events}, pages {total_pages})")

    print("\n=== EVENT COMMANDS USED (most common first) ===")
    for code, n in codes.most_common():
        print(f"{code:4d}  {CODE_NAMES.get(code, '???'):22s} {n:7d}   e.g. {where[code]}")

    print("\n=== PLUGIN COMMANDS (356) ===")
    for name, n in plugin_cmds.most_common():
        print(f"{n:6d}  {name}")

    print(f"\n=== SCRIPT CALLS (355): {script_calls} ===")
    for s in script_samples: print("  " + s)

    print("\n=== MOVE ROUTE SUB-COMMANDS ===")
    print(", ".join(f"{k}:{v}" for k, v in sorted(move_codes.items())))

    # Damage formulas: count distinct shapes (numbers stripped)
    shapes = Counter()
    for s in load(os.path.join(d, "Skills.json")):
        if s and s.get("damage", {}).get("type", 0):
            f = s["damage"].get("formula", "")
            shapes[re.sub(r"\d+(\.\d+)?", "N", f)] += 1
    print(f"\n=== DAMAGE FORMULA SHAPES: {len(shapes)} distinct ===")
    for f, n in shapes.most_common(40):
        print(f"{n:5d}  {f[:90]}")

    # Notetags drive most plugin behaviour
    tags = Counter()
    for n in ["Skills","Items","Weapons","Armors","Enemies","States","Actors"]:
        try:
            for x in load(os.path.join(d, n + ".json")):
                if x and x.get("note"):
                    for t in re.findall(r"<([A-Za-z][A-Za-z ]*?)[:>]", x["note"]):
                        tags[f"{n}:{t.strip()}"] += 1
        except FileNotFoundError:
            pass
    print("\n=== NOTETAGS ===")
    for t, n in tags.most_common(60):
        print(f"{n:5d}  {t}")

    try:
        plugins = open(os.path.join(d, "..", "js", "plugins.js"), encoding="utf-8").read()
        names = re.findall(r'"name"\s*:\s*"([^"]+)"\s*,\s*"status"\s*:\s*true', plugins)
        print(f"\n=== ACTIVE PLUGINS: {len(names)} ===")
        for n in names: print("  " + n)
    except FileNotFoundError:
        print("\n(plugins.js not found next to data/)")

if __name__ == "__main__":
    main()