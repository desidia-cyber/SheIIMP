import re, os, glob

OUT = os.path.dirname(os.path.abspath(__file__))


def strip(h):
    h = re.sub(r"(?s)<[^>]+>", "", h)
    h = (h.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
          .replace("&quot;", '"').replace("&#39;", "'").replace("&nbsp;", " "))
    return re.sub(r"\s+", " ", h).strip()


lines = []
for p in sorted(glob.glob(os.path.join(OUT, "*.html"))):
    html = open(p, encoding="utf-8", errors="replace").read()
    name = os.path.basename(p)[:-5]
    m = re.search(r"<title>(.*?)</title>", html, re.S)
    title = strip(m.group(1)) if m else ""
    heads = [strip(x) for x in re.findall(r"(?is)<h([123])[^>]*>(.*?)</h\1>", html)[0:0] or []]
    hs = []
    for mm in re.finditer(r"(?is)<h([123])[^>]*>(.*?)</h\1>", html):
        t = strip(mm.group(2))
        if t and t not in hs:
            hs.append(t)
    lines.append("## %s | %s" % (name, title))
    for t in hs:
        lines.append("   - " + t)
    lines.append("")

open(os.path.join(OUT, "headings.md"), "w", encoding="utf-8").write("\n".join(lines))
print("wrote headings for", len(glob.glob(os.path.join(OUT, "*.html"))), "pages")
