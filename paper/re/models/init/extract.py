import pickle, sys, unicorn
src, dst = sys.argv[1], sys.argv[2]
s = pickle.load(open(src, 'rb'))
open(dst, 'wb').write(s['mem'])
open(dst + '.pal', 'wb').write(s['pal'])
print(s['tick'], hex(s['eip']), {k: s['st'][k] for k in s['st'] if k != 'vga'})
