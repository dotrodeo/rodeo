#!/usr/bin/env python3
"""Look up parts in JLCPCB's assembly library (JLC's own stock, not LCSC's).

  jlc_parts.py stock C25744 C157915 ...      # model, package, JLC stock, basic/extended
  jlc_parts.py search "BM05B-SRSS-TB" [-n 8]  # best-stocked matches for a keyword

LCSC and JLC keep separate inventories; for assembly only JLC's counts. This uses
JLC's parts-search endpoint, the one behind jlcpcb.com/parts.
"""
import argparse
import json
import urllib.request

URL = 'https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList'


def query(keyword, n=10):
    req = urllib.request.Request(URL, data=json.dumps({'keyword': keyword, 'currentPage': 1, 'pageSize': n}).encode(),
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=30) as r:
        data = json.load(r)
    return data['data']['componentPageInfo']['list'] or []


def show(p):
    print(f"{p['componentCode']:10} {p['componentModelEn'][:28]:28} {p['componentSpecificationEn'][:18]:18} "
          f"{p['stockCount']:>10}  {p.get('componentLibraryType', ''):8} {(p.get('describe') or '')[:70]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['stock', 'search'])
    ap.add_argument('terms', nargs='+')
    ap.add_argument('-n', type=int, default=6)
    a = ap.parse_args()
    if a.cmd == 'stock':
        for code in a.terms:
            hit = [p for p in query(code, 5) if p['componentCode'] == code]
            show(hit[0]) if hit else print(f'{code:10} not found at JLC')
    else:
        for p in sorted(query(' '.join(a.terms), 30), key=lambda p: -p['stockCount'])[:a.n]:
            show(p)


if __name__ == '__main__':
    main()
