"""Builds docs/concerts/*.json: upcoming shows for every artist in concerts/artists.json.

Sources (all optional, merged; duplicates of the same show are dropped):
  1. Bandsintown via the Apify actor solidcode/bandsintown-scraper, if env APIFY_TOKEN is set
     (repository secret of the same name). Worldwide dates with exact local start times. It's billed
     per event returned (~$1.20 per 1,000), so only artists marked "apify": true in artists.json are
     looked up. Bandsintown times carry no time zone, so each city's zone comes from the free
     Open-Meteo geocoder.
  2. Ticketmaster Discovery API, if env TM_API_KEY is set.
  3. concerts/manual.json, for shows the others miss.
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
APIFY = os.environ.get("APIFY_TOKEN", "").strip()
APIFY_TASK = os.environ.get("APIFY_TASK", "").strip()   # a saved Apify task to run instead of the actor below
APIFY_ACTOR = "https://api.apify.com/v2/acts/solidcode~bandsintown-scraper/run-sync-get-dataset-items"
API = "https://app.ticketmaster.com/discovery/v2/"
UA = {"User-Agent": "holocube-concerts/1.0"}
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


_tz_cache = {}


def city_tz(city, country):
    """IANA time zone for a city via the Open-Meteo geocoder (free, no key). None if unknown."""
    key = (city, country)
    if key in _tz_cache:
        return _tz_cache[key]
    tz = None
    try:
        url = "https://geocoding-api.open-meteo.com/v1/search?" + urllib.parse.urlencode(
            {"name": city, "count": 10, "language": "en", "format": "json"})
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=20) as r:
            results = json.load(r).get("results") or []
        same = [x for x in results if norm(x.get("country", "")) == norm(country or "")]
        best = (same or results or [None])[0]
        tz = best and best.get("timezone")
    except Exception as e:
        print(f"  time zone lookup failed for {city}, {country}: {e}", file=sys.stderr)
    _tz_cache[key] = tz
    return tz


def _get(d, *paths):
    """First non-empty value among dotted paths, e.g. _get(row, "venueCity", "venue.city")."""
    for p in paths:
        v = d
        for k in p.split("."):
            v = v.get(k) if isinstance(v, dict) else None
        if v not in (None, "", []):
            return v
    return None


def bandsintown_events(artists):
    """One Apify run for all opted-in artists. Returns {artist slug: [events]}; raises on failure.

    With APIFY_TASK set, runs that saved Apify task with its own input; otherwise calls the
    solidcode/bandsintown-scraper actor with the opted-in artists."""
    auth = {**UA, "Authorization": f"Bearer {APIFY}"}
    if APIFY_TASK:
        base = f"https://api.apify.com/v2/actor-tasks/{APIFY_TASK}"
        req = urllib.request.Request(base + "/run-sync-get-dataset-items?timeout=280", data=b"", method="POST", headers=auth)
    else:
        base = APIFY_ACTOR.rsplit("/", 1)[0]
        body = json.dumps({"artists": [a["name"] for a in artists], "queryType": "events",
                           "dateFilter": "upcoming", "includeEventDetails": False, "maxResults": 60 * len(artists)})
        req = urllib.request.Request(APIFY_ACTOR + "?timeout=280", data=body.encode(), method="POST",
                                     headers={**auth, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=300) as r:
        rows = json.load(r)

    def artist_of(row):
        lineup = row.get("lineup") or []
        return _get(row, "artistName", "artist.name", "artist", "artist_name") or (lineup[0] if lineup and isinstance(lineup[0], str) else None)

    names = {}
    for row in rows:
        n = str(artist_of(row) or "?")
        names[n] = names.get(n, 0) + 1
    print(f"  Bandsintown ({'task ' + APIFY_TASK if APIFY_TASK else 'actor'}) returned {len(rows)} row(s): "
          + ", ".join(f"{n} x{c}" for n, c in names.items()))
    if rows:
        print(f"  first row fields: {sorted(rows[0].keys())}")
    if not rows:   # show why: the actor's own run status and log tail
        try:
            base = base + "/runs/last"
            with urllib.request.urlopen(urllib.request.Request(base, headers=auth), timeout=30) as r:
                run = json.load(r).get("data", {})
            print(f"  Apify run status: {run.get('status')} {run.get('statusMessage') or ''}")
            with urllib.request.urlopen(urllib.request.Request(base + "/log", headers=auth), timeout=30) as r:
                print("  Apify log (last 40 lines):\n    " + "\n    ".join(r.read().decode("utf-8", "replace").splitlines()[-40:]))
        except Exception as e:
            print(f"  (couldn't read the Apify run log: {e})", file=sys.stderr)

    def match(name):   # Bandsintown spellings vary ("TXT", "TOMORROW X TOGETHER (투모로우바이투게더)")
        n = norm(name or "")
        if not n:
            return None
        for a in artists:
            keys = {norm(a["name"]), norm(a["short"])}
            if n in keys or any(k and (k in n or n in k) and min(len(k), len(n)) >= 3 for k in keys):
                return a["slug"]
        return None

    only = artists[0]["slug"] if len(artists) == 1 else None   # rows without an artist name belong to it
    out = {a["slug"]: [] for a in artists}
    for row in rows:
        name = artist_of(row)
        slug = match(str(name)) if name else only
        local = _get(row, "datetime", "starts_at", "startsAt", "startDate", "date")
        if row.get("recordType") not in (None, "event") or not slug or not local:
            continue
        city = _get(row, "venueCity", "venue.city", "city") or ""
        country = _get(row, "venueCountry", "venue.country", "country") or ""
        try:
            local_dt = datetime.fromisoformat(str(local).replace("Z", "+00:00")[:25])
        except ValueError:
            print(f"  {slug}: can't read date {local!r}", file=sys.stderr)
            continue
        if local_dt.tzinfo is None:   # Bandsintown gives venue-local time without a zone
            tz = city_tz(city, country)
            if not tz:
                print(f"  {slug}: no time zone for {city}, {country}; assuming Eastern", file=sys.stderr)
            local_dt = local_dt.replace(tzinfo=ZoneInfo(tz) if tz else ET)
        if len(str(local)) <= 10:     # date only, no time: assume a 7 PM start
            local_dt = local_dt.replace(hour=19)
        offers = row.get("offers") or []
        out[slug].append({"et": local_dt.astimezone(ET).strftime("%Y-%m-%dT%H:%M"), "city": city, "country": country,
                          "venue": _get(row, "venueName", "venue.name") or (row["venue"] if isinstance(row.get("venue"), str) else ""),
                          "url": (offers[0].get("url") if offers and isinstance(offers[0], dict) else None) or row.get("url") or ""})
    return out


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
    if not KEY and not APIFY:
        print("Neither APIFY_TOKEN nor TM_API_KEY set: only concerts/manual.json entries will be used.", file=sys.stderr)
    if (KEY or APIFY) and not ET:
        sys.exit("Time zone database missing (pip install tzdata).")
    labels = json.loads((ROOT / "concerts" / "artists.json").read_text(encoding="utf-8"))["labels"]
    manual = json.loads((ROOT / "concerts" / "manual.json").read_text(encoding="utf-8"))
    OUT.mkdir(parents=True, exist_ok=True)
    now_et = (datetime.now(timezone.utc).astimezone(ET) if ET else datetime.now()) - timedelta(hours=6)
    cutoff = now_et.strftime("%Y-%m-%dT%H:%M")   # keep a show listed until 6 h after it starts
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%MZ")

    bit, bit_failed = {}, False
    bit_artists = [a for artists in labels.values() for a in artists if a.get("apify")]
    if APIFY and bit_artists:
        try:
            bit = bandsintown_events(bit_artists)
        except urllib.error.HTTPError as e:
            bit_failed = True
            print(f"Bandsintown (Apify) failed: HTTP {e.code} {e.read()[:500]!r}", file=sys.stderr)
        except Exception as e:   # keep yesterday's files for these artists rather than wiping them
            bit_failed = True
            print(f"Bandsintown (Apify) failed: {e}", file=sys.stderr)
    sources = [s for s, on in (("Bandsintown", APIFY and bit_artists), ("Ticketmaster", KEY)) if on]
    index = {"updated": stamp, "source": " + ".join(sources) or "manual only", "labels": {}}
    changed = 0
    for label, artists in labels.items():
        index["labels"][label] = []
        for a in artists:
            events = list(bit.get(a["slug"], []))
            old = OUT / f"{a['slug']}.json"
            if bit_failed and a.get("apify") and old.exists():
                events = [e for e in json.loads(old.read_text(encoding="utf-8")).get("events", []) if e.get("et")]
            if KEY:
                try:
                    events += ticketmaster_events(a)
                except Exception as e:   # one bad artist shouldn't sink the run; keep the old file
                    print(f"  {a['short']}: {e}", file=sys.stderr)
                    if old.exists() and not events:
                        events = json.loads(old.read_text(encoding="utf-8")).get("events", [])
            events += [dict(e, url=e.get("url", "")) for e in manual.get(a["slug"], [])]
            # Same show from two sources: same city within 3 hours (sources round start times differently).
            uniq = []
            for e in sorted(events, key=lambda e: e["et"]):
                if e["et"] < cutoff:
                    continue
                t = datetime.fromisoformat(e["et"])
                if any(norm(u.get("city", "")) == norm(e.get("city", "")) and
                       abs(datetime.fromisoformat(u["et"]) - t) <= timedelta(hours=3) for u in uniq):
                    continue
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
