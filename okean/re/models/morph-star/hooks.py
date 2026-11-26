"""Read hookrun.py's log: for every copy to video memory (record F of the capture), the callback count the frame
was drawn with (callbacks before the main loop's start of that frame), the number of the drawn frame (0-based) and
the variables at the start of the frame."""


def parse(path):
    out = []
    top = None
    for line in open(path):
        f = line.split()
        if f[0] == 'top':
            top = dict(n=int(f[1]) - 1, frame=int(f[3]), cbs=int(f[7]), pos=f[9],
                       vars={k: int(v) for k, v in (x.split('=') for x in f[10:])})
        elif f[0] == 'copy' and top is not None:
            out.append(dict(record=int(f[2]), cbs_copy=int(f[6]), pos_copy=f[10],
                            vars_copy={k: int(v) for k, v in (x.split('=') for x in f[11:])}, **top))
    return out
