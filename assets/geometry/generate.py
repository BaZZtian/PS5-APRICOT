# SPDX-License-Identifier: MIT
"""Create independently authored generic cartridge geometry; no source model or game input.

APCM0001 is the existing native menu format. The title texture is supplied locally
at runtime and is deliberately absent from this geometry. Never overwrites output.
"""
import argparse
import math
import struct
from pathlib import Path


def generate():
    vertices, triangles = [], []
    def vertex(x, y, z, normal, uv=(0, 0)):
        vertices.append((x, y, z, *normal, *uv))
        return len(vertices)-1
    def face(points, normal, material):
        ids = [vertex(*point[:3], normal, point[3:] if len(point)==5 else (0, 0)) for point in points]
        for i in range(1, len(ids)-1):
            triangles.append((ids[0], ids[i], ids[i+1], material))
    # A simple bevelled rectangle; authored dimensions, no manufacturer marks.
    outline = [(-.42,-.50),(.42,-.50),(.50,-.42),(.50,.42),
               (.42,.50),(-.42,.50),(-.50,.42),(-.50,-.42)]
    face([(x,.055,z) for x,z in outline], (0,1,0), 0)
    face([(x,-.055,z) for x,z in reversed(outline)], (0,-1,0), 1)
    for i,(x,z) in enumerate(outline):
        a,b = outline[(i+1)%len(outline)]
        dx,dz=a-x,b-z;length=math.hypot(dx,dz)
        face([(x,-.055,z),(a,-.055,b),(a,.055,b),(x,.055,z)], (dz/length,0,-dx/length), 1)
    # Material4 is the user's local label. Front upper-left is UV(0,0).
    face([(-.36,.058,.34,0,0),(.36,.058,.34,1,0),
          (.36,.058,-.20,1,1),(-.36,.058,-.20,0,1)], (0,1,0), 4)
    # Authored contact pads on the back; no text, logos, textures or mesh imports.
    for i in range(8):
        x=-.35+i*.09
        face([(x,-.058,-.43),(x+.055,-.058,-.43),
              (x+.055,-.058,-.23),(x,-.058,-.23)], (0,-1,0), 2)
    header=b'APCM0001'+struct.pack('<6I',len(vertices),len(triangles),5,0,0,0)
    return header+b''.join(struct.pack('<8f',*v) for v in vertices)+b''.join(struct.pack('<4I',*t) for t in triangles)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    data=generate()
    with args.output.open('xb') as output:
        output.write(data)
    print('Authored generic mesh:',len(data),'bytes; no game or third-party model data')


if __name__=='__main__':
    main()
