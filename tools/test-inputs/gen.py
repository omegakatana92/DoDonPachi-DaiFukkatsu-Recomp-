import random, sys
# Input scripts for the comparison runs against MAME (tools/cv1k_reference.lua).
# usage: python3 gen.py SEED FRAMES [two] > file   ("FRAME MASK" lines, cv1k::IN_* bits)
#   python3 gen.py 1 12000 > p1.txt ; python3 gen.py 2 12000 two > p2.txt
# The first boot (fresh EEPROM) shows the title from about frame 1700.
seed = int(sys.argv[1]); N = int(sys.argv[2]); two = len(sys.argv) > 3
r = random.Random(seed)
UP, DN, LT, RT, B1, B2, B3, B4 = 1, 2, 4, 8, 16, 32, 64, 128
P2 = 8
COIN1 = 1 << 16; ST1 = 1 << 18; ST2 = 1 << 19
ev = []
def at(fr, m): ev.append((fr, m))
at(0, 0)
for k in range(3 if two else 2):
    at(1900 + 20 * k, COIN1); at(1906 + 20 * k, 0)
at(2000, ST1); at(2006, 0)
if two: at(2020, ST2); at(2026, 0)
f = 2100
while f < N:
    d = r.choice([0, UP, DN, LT, RT, UP | LT, UP | RT, DN | LT, DN | RT, LT, RT])
    b = r.choice([B1, B1, B1, B1 | B3, B3, B1, B2 if r.random() < 0.08 else B1, B4 if r.random() < 0.05 else B1])
    m = d | b
    if two:
        d2 = r.choice([0, UP, DN, LT, RT]); m |= (d2 | B1) << P2
    L = r.randint(8, 60)
    at(f, m); f += L
    # now and then a coin and start, to continue
    if r.random() < 0.03:
        at(f, COIN1); at(f + 6, 0); at(f + 30, ST1 | (ST2 if two else 0)); at(f + 36, 0); f += 60
for fr, m in ev: print(fr, m)
