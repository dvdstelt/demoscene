from common import *
from PIL import Image
s = snap('2c26f')
print(s.keys(), s['pm'] if 'pm' in s else None)
m_file = np.frombuffer(frd(0x1502c, 64000), np.uint8).reshape(200, 320)
m_snap = np.frombuffer(mem(s, 0x1502c, 64000), np.uint8).reshape(200, 320)
print('file nonzero', np.count_nonzero(m_file), 'snap nonzero', np.count_nonzero(m_snap), 'equal', (m_file == m_snap).all())
print('values', np.unique(m_file, return_counts=True))
Image.fromarray((m_file > 0).astype(np.uint8) * 255).save(os.path.join(HERE, 'title_mask_1502c.png'))
print('vars', [hex(mi32(s, a)) for a in (0x15020, 0x15024, 0x15028, 0x29a40, 0x29a44, 0x14d1c, 0x4c8e4, 0x4c8dc, 0x3ceb0)])
print('vars file', [hex(fi32(a)) for a in (0x15020, 0x15024, 0x15028, 0x29a40, 0x29a44, 0x14d1c, 0x4c8e4, 0x4c8dc, 0x3ceb0)])
