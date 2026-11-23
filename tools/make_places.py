"""Builds data/places.bin and data/airports.bin, embedded in the firmware.

Sources (both free / open):
  - GeoNames cities (CC-BY 4.0) via the `geonamescache` Python package:
    every town of 5,000+ people in North America & the Caribbean, and
    every city of 15,000+ worldwide.
  - OurAirports (public domain): airports with scheduled passenger service
    and an IATA code (JFK, LGA, ...).

Run:  pip install geonamescache  &&  python tools/make_places.py path/to/airports.csv
(airports.csv from https://github.com/davidmegginson/ourairports-data)

places.bin  record: int32 lat*1e5, int32 lon*1e5, uint8 size code, uint8 len, name
            (sorted biggest first; size code = round(log2(population) * 8))
airports.bin record: int32 lat*1e5, int32 lon*1e5, uint8 kind (2 large, 1 medium), char[3] IATA,
             uint8 len, city name (ASCII, up to 24 chars)
"""
import csv, json, math, os, struct, sys, unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NA = {'US', 'CA', 'MX', 'PR', 'VI', 'BS', 'CU', 'JM', 'DO', 'HT', 'GT', 'BZ', 'HN', 'SV', 'NI', 'CR', 'PA',
      'TT', 'BB', 'AG', 'KN', 'LC', 'VC', 'GD', 'DM', 'AW', 'CW', 'KY', 'TC', 'BM'}

def ascii_name(s):
    return unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode().strip()

def main(airports_csv):
    import geonamescache
    path = os.path.join(os.path.dirname(geonamescache.__file__), 'data', 'cities5000.json')
    cities = list(json.load(open(path, encoding='utf8')).values())
    rows = []
    for c in cities:
        if not (c['countrycode'] in NA or c['population'] >= 15000):
            continue
        name = ascii_name(c['name'])
        if not name or len(name) > 40:
            continue
        pop = max(int(c['population']), 1)
        rows.append((pop, c['latitude'], c['longitude'], name))
    rows.sort(key=lambda r: -r[0])
    with open(os.path.join(ROOT, 'data', 'places.bin'), 'wb') as f:
        for pop, lat, lon, name in rows:
            code = min(255, round(math.log2(pop) * 8))
            b = name.encode()
            f.write(struct.pack('<iiBB', round(lat * 1e5), round(lon * 1e5), code, len(b)) + b)

    ap = []
    for r in csv.DictReader(open(airports_csv, encoding='utf8')):
        if r['type'] not in ('large_airport', 'medium_airport') or r['scheduled_service'] != 'yes':
            continue
        code = r['iata_code'].strip().upper()
        if len(code) != 3 or not code.isalpha():
            continue
        kind = 2 if r['type'] == 'large_airport' else 1
        city = ascii_name(r['municipality'] or r['name'])[:24]
        ap.append((kind, float(r['latitude_deg']), float(r['longitude_deg']), code, city))
    ap.sort(key=lambda r: -r[0])
    with open(os.path.join(ROOT, 'data', 'airports.bin'), 'wb') as f:
        for kind, lat, lon, code, city in ap:
            c = city.encode()
            f.write(struct.pack('<iiB', round(lat * 1e5), round(lon * 1e5), kind) + code.encode() + bytes([len(c)]) + c)
    print(f'{len(rows)} places, {len(ap)} airports')

if __name__ == '__main__':
    main(sys.argv[1])
