"""Builds docs/concerts/*.json: upcoming shows for every artist in concerts/artists.json.

Source: Ticketmaster Discovery API (free key from developer.ticketmaster.com, set as env TM_API_KEY;
in GitHub it's the repository secret of the same name). Entries in concerts/manual.json are merged in.
Runs daily from .github/workflows/concerts.yml. Standard library only.

Output per artist (what the cube downloads):
  {"artist": "TXT", "name": "TOMORROW X TOGETHER", "label": "HYBE",
   "events": [{"et": "2026-12-20T19:00", "city": "Chicago", "country": "US", "venue": "...", "url": "..."}]}
et is the start time converted to US Eastern, which is what the countdown uses.
"""
import json, os, re, sys, time, urllib.parse, urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path

try:
    from zoneinfo import ZoneInfo
    ET = ZoneInfo("America/New_York")
except Exception:          # no tz database (e.g. Windows without tzdata): only manual entries work
    ZoneInfo = ET = None

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "concerts"
KEY = os.environ.get("TM_API_KEY", "").strip()
API = "https://app.ticketmaster.com/discovery/v2/"
SKIP = re.compile(r"parking|upgrade|vip package|merch|shuttle|hotel|soundcheck experience", re.I)


def norm(s):
    return re.sub(r"[^a-z0-9&]", "", s.casefold())


def tm(path, **params):
    params["apikey"] = KEY
    url = API + path + "?" + urllib.parse.urlencode(params)
    for attempt in range(4):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "holocube-concerts"}), timeout=20) as r:
                time.sleep(0.25)   # stay under 5 requests/second
                return json.load(r)
        except urllib.error.HTTPError as e:
            if e.code == 429 and attempt < 3:
                time.sleep(2 + attempt * 2)
                continue
            raise


def to_et(ev):
    s = ev.get("dates", {}).get("start", {})
    if s.get("dateTime"):
        t = datetime.fromisoformat(s["dateTime"].replace("Z", "+00:00"))
    elif s.get("localDate"):
        tz = ev.get("dates", {}).get("timezone")
        local = datetime.fromisoformat(s["localDate"] + "T" + (s.get("localTime") or "19:00:00"))
        t = local.replace(tzinfo=ZoneInfo(tz) if tz else ET)
    else:
        return None
    return t.astimezone(ET)


def ticketmaster_events(artist):
    found = tm("attractions.json", keyword=artist["name"], size=20, classificationName="music")
    want = norm(artist.get("tm", artist["name"]))
    ids = [a["id"] for a in found.get("_embedded", {}).get("attractions", []) if norm(a["name"]) == want]
    if not ids:
        print(f"  {artist['short']}: not on Ticketmaster")
        return []
    events = []
    for page in range(3):
        res = tm("events.json", attractionId=ids[0], size=100, page=page, sort="date,asc")
        for ev in res.get("_embedded", {}).get("events", []):
            if SKIP.search(ev.get("name", "")):
                continue
            t = to_et(ev)
            if not t:
                continue
            v = (ev.get("_embedded", {}).get("venues") or [{}])[0]
            events.append({"et": t.strftime("%Y-%m-%dT%H:%M"), "city": v.get("city", {}).get("name", ""),
                           "country": v.get("country", {}).get("countryCode", ""), "venue": v.get("name", ""),
                           "url": ev.get("url", "")})
        if page + 1 >= res.get("page", {}).get("totalPages", 1):
            break
    return events


def write_if_changed(path, data):
    """Skip rewriting when only the timestamp would change, so the repo gets a commit only on real news."""
    body = {k: v for k, v in data.items() if k != "updated"}
    if path.exists():
        try:
            old = json.loads(path.read_text(encoding="utf-8"))
            if {k: v for k, v in old.items() if k != "updated"} == body:
                return False
        except ValueError:
            pass
    path.write_text(json.dumps(data, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return True


def main():
    if not KEY:
        print("TM_API_KEY not set: only concerts/manual.json entries will be used.", file=sys.stderr)
    if KEY and not ET:
        sys.exit("Time zone database missing (pip install tzdata).")
    labels = json.loads((ROOT / "concerts" / "artists.json").read_text(encoding="utf-8"))["labels"]
    manual = json.loads((ROOT / "concerts" / "manual.json").read_text(encoding="utf-8"))
    OUT.mkdir(parents=True, exist_ok=True)
    now_et = (datetime.now(timezone.utc).astimezone(ET) if ET else datetime.now()) - timedelta(hours=6)
    cutoff = now_et.strftime("%Y-%m-%dT%H:%M")   # keep a show listed until 6 h after it starts
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%MZ")
    index = {"updated": stamp, "source": "Ticketmaster" if KEY else "manual only", "labels": {}}
    changed = 0
    for label, artists in labels.items():
        index["labels"][label] = []
        for a in artists:
            events = []
            if KEY:
                try:
                    events = ticketmaster_events(a)
                except Exception as e:   # one bad artist shouldn't sink the run; keep the old file
                    print(f"  {a['short']}: {e}", file=sys.stderr)
                    old = OUT / f"{a['slug']}.json"
                    if old.exists():
                        events = json.loads(old.read_text(encoding="utf-8")).get("events", [])
            events += [dict(e, url=e.get("url", "")) for e in manual.get(a["slug"], [])]
            seen, uniq = set(), []
            for e in sorted(events, key=lambda e: e["et"]):
                k = (e["et"], norm(e.get("venue", "")))
                if e["et"] >= cutoff and k not in seen:
                    seen.add(k)
                    uniq.append(e)
            uniq = uniq[:30]
            changed += write_if_changed(OUT / f"{a['slug']}.json", {
                "updated": stamp, "artist": a["short"], "name": a["name"], "label": label, "events": uniq})
            nxt = uniq[0] if uniq else None
            index["labels"][label].append({"slug": a["slug"], "short": a["short"], "name": a["name"],
                                           "next": {k: nxt[k] for k in ("et", "city", "venue")} if nxt else None,
                                           "count": len(uniq)})
            print(f"  {label:5} {a['short']:15} {len(uniq):3} upcoming" + (f", next {nxt['et']} {nxt['city']}" if nxt else ""))
    changed += write_if_changed(OUT / "index.json", index)
    print(f"{changed} file(s) changed")


if __name__ == "__main__":
    main()
