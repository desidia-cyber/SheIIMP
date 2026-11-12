import re, os, glob, json, urllib.request

OUT = os.path.dirname(os.path.abspath(__file__))
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0 Safari/537.36"

# 1) sitemap
urls = []
for sm in ("sitemap-0.xml", "sitemap-index.xml"):
    p = os.path.join(OUT, sm)
    if not os.path.exists(p):
        try:
            req = urllib.request.Request("https://mortalshell2.wiki/" + sm, headers={"User-Agent": UA})
            d = urllib.request.urlopen(req, timeout=30).read()
            open(p, "wb").write(d)
            print("got", sm, len(d))
        except Exception as e:
            print("ERR", sm, e)
            continue
    txt = open(p, encoding="utf-8", errors="replace").read()
    urls += re.findall(r"<loc>(.*?)</loc>", txt)
urls = sorted(set(urls))
open(os.path.join(OUT, "urls.txt"), "w", encoding="utf-8").write("\n".join(urls))
print("sitemap urls:", len(urls))

# 2) html -> text
def to_text(html):
    h = re.sub(r"(?is)<(script|style|nav|footer|svg)[^>]*>.*?</\1>", " ", html)
    h = re.sub(r"(?i)<(br|/p|/div|/li|/h[1-6]|/tr|/td)[^>]*>", "\n", h)
    h = re.sub(r"(?s)<[^>]+>", " ", h)
    h = (h.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
          .replace("&quot;", '"').replace("&#39;", "'").replace("&nbsp;", " "))
    h = re.sub(r"[ \t]+", " ", h)
    h = re.sub(r"\n\s*\n+", "\n", h)
    return h.strip()

rows = []
for p in sorted(glob.glob(os.path.join(OUT, "*.html"))):
    html = open(p, encoding="utf-8", errors="replace").read()
    t = to_text(html)
    tp = p[:-5] + ".txt"
    open(tp, "w", encoding="utf-8").write(t)
    m = re.search(r"<title>(.*?)</title>", html, re.S)
    title = re.sub(r"\s+", " ", m.group(1)).strip() if m else ""
    rows.append((os.path.basename(p), len(t), title))

rows.sort(key=lambda r: -r[1])
with open(os.path.join(OUT, "pages.tsv"), "w", encoding="utf-8") as f:
    for r in rows:
        f.write("%s\t%d\t%s\n" % r)
print("pages converted:", len(rows))
