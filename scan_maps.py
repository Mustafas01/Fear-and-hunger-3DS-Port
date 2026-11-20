import json, sys, os

www = os.path.join(sys.argv[1], "www", "data")
infos = json.load(open(os.path.join(www, "MapInfos.json"), encoding="utf-8"))

for info in infos:
    if not info:
        continue
    i = info["id"]
    p = os.path.join(www, "Map%03d.json" % i)
    if not os.path.exists(p):
        continue
    m = json.load(open(p, encoding="utf-8"))
    w, h, d = m["width"], m["height"], m["data"]
    n = sum(1 for v in d[:w * h * 4] if v)
    ev = len([e for e in m["events"] if e])
    print(i, "|", info["name"], "|", "%dx%d" % (w, h), "| tiles:", n,
          "| events:", ev, "| parallax:", m["parallaxName"] or "-")