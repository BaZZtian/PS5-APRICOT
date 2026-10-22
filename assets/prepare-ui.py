"""Build ApricotGS-owned display assets. No game inputs are read."""
from pathlib import Path
import importlib.util
import shutil
import sys
from PIL import Image

root = Path(__file__).resolve().parents[1]
destination = Path(sys.argv[1]).resolve()
(destination/'userdata').mkdir(parents=True, exist_ok=True)
(destination/'sce_sys').mkdir(parents=True, exist_ok=True)
for source, name in [('banner.png', 'launcher-uhd.rgba'),
                     ('field-master.png', 'field-uhd.rgba')]:
    with Image.open(root/'assets/branding'/source) as image:
        result = image.convert('RGBA').resize((3840, 2160), Image.Resampling.LANCZOS)
        (destination/'userdata'/name).write_bytes(result.tobytes())
shutil.copyfile(root/'assets/geometry/generic-cartridge.mesh', destination/'userdata/cartridge.mesh')
spec = importlib.util.spec_from_file_location('apricot_dds', root/'platform/ps5/tools/png-to-title-dds.py')
dds = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dds)
import numpy as np
with Image.open(root/'assets/branding/banner.png') as image:
    rgb = image.convert('RGB').resize((dds.WIDTH, dds.HEIGHT), Image.Resampling.LANCZOS)
    data = dds.dds_header()+dds.bc7(np.asarray(rgb, dtype=np.float64))
for name in ('pic0.dds', 'pic1.dds'):
    (destination/'sce_sys'/name).write_bytes(data)
