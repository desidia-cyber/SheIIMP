import re, os, sys, time, urllib.request, urllib.parse, hashlib

BASE = "https://mortalshell2.wiki"
OUT = os.path.dirname(os.path.abspath(__file__))
UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0 Safari/537.36")

seen = set()
queue = []
fetched = {}


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA,
                                               "Accept": "text/html,application/xhtml+xml"})
    with urllib.request.urlopen(req, timeout=40) as r:
        return r.status, r.read()


def fname(url):
    p = urllib.parse.urlparse(url).path.strip("/")
    if not p:
        return "_index.html"
    p = p.replace("/", "__")
    p = re.sub(r"[^A-Za-z0-9_.-]", "_", p)
    return p + (".html" if not p.endswith(".html") else "")


def links(html):
    out = []
    for m in re.finditer(r'href="([^"#]+)"', html):
        u = m.group(1)
        if u.startswith("mailto:") or u.startswith("javascript:"):
            continue
        out.append(urllib.parse.urljoin(BASE + "/", u))
    return out


def main(limit):
    queue.append(BASE + "/")
    while queue and len(fetched) < limit:
        url = queue.pop(0)
        key = url.split("#")[0].rstrip("/")
        if key in seen:
            continue
        seen.add(key)
        if not key.startswith(BASE):
            continue
        try:
            st, data = get(key)
        except Exception as e:
            print("ERR", key, type(e).__name__, e)
            continue
        html = data.decode("utf-8", "replace")
        fn = os.path.join(OUT, fname(key))
        with open(fn, "w", encoding="utf-8") as f:
            f.write(html)
        text = re.sub(r"(?is)<(script|style)[^>]*>.*?</\1>", " ", html)
        text = re.sub(r"(?s)<[^>]+>", " ", text)
        text = re.sub(r"\s+", " ", text)
        fetched[key] = (st, len(html), len(text))
        print("OK %-70s %6d  %s" % (key.replace(BASE, "") or "/", len(html), text[:90]))
        for l in links(html):
            lk = l.split("#")[0].rstrip("/")
            if lk.startswith(BASE) and lk not in seen:
                queue.append(lk)
        time.sleep(0.25)


if __name__ == "__main__":
    lim = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    main(lim)
    print("fetched", len(fetched))
