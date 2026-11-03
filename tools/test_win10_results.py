#!/usr/bin/env python3
"""Unit test for win10_results.py with a fabricated log. Run: python3 tools/test_win10_results.py"""
import os, sys, unittest
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import win10_results as W

LOG = """boot noise
W32K begin modes=adm,std exes=4 timeout=60
W32K os build=19045 version=10.0.19045 sku=48 arch=64-bit
W32K mode adm
W32K start a
W32K out a | RUN  t1
W32K out a | OK   t1
W32K out a |   FAIL x.c:3: CHECK(x) (got 1)
W32K out a | FAIL t2
W32K out a | SKIP t3
W32K out a |   SKIP no touch
W32K out a | SUMMARY tests=3 failed=1 skipped=1 checks_pass=5 checks_fail=1
W32K rc a 1 1.5
W32K start b
W32K out b | OK   u1
W32K rc b 0xc0000005 0.2
W32K start c
W32K rc c TIMEOUT 60
W32K mode std
W32K start d
W32K out d | OK   v1
"""
class T(unittest.TestCase):
    def test_parse(self):
        r = W.parse(LOG); runs = {(x["mode"], x["exe"]): x for x in r["runs"]}
        self.assertFalse(r["done"])
        a = runs[("adm", "a")]
        self.assertEqual((a["tests"], a["failed"], a["skipped"], a["checks_fail"], a["status"]), (3, 1, 1, 1, "OK"))
        self.assertEqual(a["results"], [("OK", "t1"), ("FAIL", "t2"), ("SKIP", "t3")])
        self.assertEqual(runs[("adm", "b")]["status"], "NOSUMMARY")
        self.assertEqual(runs[("adm", "c")]["status"], "TIMEOUT")
        self.assertEqual(runs[("std", "d")]["status"], "HANG")
if __name__ == "__main__": unittest.main()
