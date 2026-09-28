# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract exact raw frames and viewable images selected by slow-motion playback time."""
from __future__ import annotations
import argparse
from bisect import bisect_left
import csv
from fractions import Fraction
import json
from pathlib import Path

import numpy as np
from PIL import Image

from .beam_composite import native_gray
from .convert import ConversionError, load_run, sha256, write_json


def nearest(timestamps, timestamp):
    j = bisect_left(timestamps,timestamp)
    return min((i for i in (j-1,j) if 0 <= i < len(timestamps)),
               key=lambda i:abs(timestamps[i]-timestamp))


def extract(run, slow_dir, output, start, end):
    if start < 0 or end < start:
        raise ConversionError('Invalid playback interval')
    metadata = json.loads((slow_dir/'composite.json').read_text())
    with (slow_dir/'frame-map.tsv').open() as stream:
        rows = [row for row in csv.DictReader(stream,delimiter='\t')
                if start <= Fraction(int(row['output_frame']),metadata['output_fps']) <= end]
    if not rows:
        raise ConversionError('No frames in requested interval')
    records = {record.name:record for record in load_run(run)}
    mira,imx = records['mira220'],records['imx296']
    indices = sorted({int(row['mira220_frame']) for row in rows})
    if indices[0] < 0 or indices[-1] >= len(mira.timestamps_us):
        raise ConversionError('Source frame index outside capture')
    matches = {i:nearest(imx.timestamps_us,mira.timestamps_us[i]) for i in indices}
    selected = {'mira220':indices,'imx296':sorted(set(matches.values()))}
    output.mkdir(exist_ok=False,parents=False)
    files=[]
    for name,selected_indices in selected.items():
        camera=records[name]
        camera_dir=output/name
        camera_dir.mkdir()
        stat=camera.raw.stat()
        with camera.raw.open('rb') as source:
            for index in selected_indices:
                source.seek(index*camera.sizeimage)
                data=source.read(camera.sizeimage)
                if len(data) != camera.sizeimage:
                    raise ConversionError('Incomplete raw frame')
                stem=f'frame-{index:06d}'
                raw_path=camera_dir/(stem+'.raw')
                raw_path.write_bytes(data)
                preview=native_gray(camera,data)
                preview_path=camera_dir/(stem+'.png')
                preview.save(preview_path)
                with Image.open(preview_path) as check:
                    if check.size != (camera.width,camera.height) or check.tobytes() != preview.tobytes():
                        raise ConversionError('PNG verification failed')
                paths=[raw_path,preview_path]
                if camera.fourcc == 'Y10P':
                    packed=np.frombuffer(data,dtype=np.uint8).reshape(camera.height,camera.stride)[:,:camera.width//4*5]
                    groups=packed.reshape(camera.height,camera.width//4,5)
                    pixels=((groups[:,:,:4].astype(np.uint16)<<2) |
                            ((groups[:,:,4:5].astype(np.uint16)>>np.array([0,2,4,6],dtype=np.uint16))&3)).reshape(camera.height,camera.width)
                    # Round-trip the active packed bytes, including all low bits.
                    regrouped=pixels.reshape(camera.height,camera.width//4,4)
                    restored=np.empty_like(groups)
                    restored[:,:,:4]=(regrouped>>2).astype(np.uint8)
                    restored[:,:,4]=np.sum((regrouped&3)<<np.array([0,2,4,6],dtype=np.uint16),axis=2).astype(np.uint8)
                    if not np.array_equal(restored,groups):
                        raise ConversionError('RAW10 unpack round-trip failed')
                    tiff_path=camera_dir/(stem+'-native10bit.tiff')
                    Image.fromarray(pixels).save(tiff_path,compression='tiff_lzw')
                    with Image.open(tiff_path) as check:
                        if not np.array_equal(np.asarray(check),pixels):
                            raise ConversionError('Native TIFF verification failed')
                    paths.append(tiff_path)
                for path in paths:
                    files.append({'path':path.relative_to(output).as_posix(),'sha256':sha256(path),'bytes':path.stat().st_size})
        current=camera.raw.stat()
        if (stat.st_size,stat.st_mtime_ns)!=(current.st_size,current.st_mtime_ns):
            raise ConversionError('Source raw changed during extraction')
    with (output/'mapping.tsv').open('w',newline='') as stream:
        writer=csv.writer(stream,delimiter='\t')
        writer.writerow(['slow_output_frame','slow_time_s','mira220_frame','imx296_nearest_frame',
                         'mira_timestamp_us','imx_timestamp_us','imx_minus_mira_us',
                         'raw_original_video_time_s','plot_original_video_time_s','analyzer_time_s'])
        for row in rows:
            i=int(row['mira220_frame']); j=matches[i]
            writer.writerow([row['output_frame'],row['output_time_s'],i,j,mira.timestamps_us[i],
                             imx.timestamps_us[j],imx.timestamps_us[j]-mira.timestamps_us[i],
                             row['raw_original_video_time_s'],row['plot_original_video_time_s'],row['analyzer_time_s']])
    manifest={'source_run':str(run),'slow_motion_source':str(slow_dir),'slow_interval_seconds':[float(start),float(end)],
              'interval_end_inclusive':True,'indices_are_zero_based':True,
              'imx_selection':'nearest recorded timestamp to each distinct Mira frame; not hardware-synchronized',
              'cameras':{name:{'width':r.width,'height':r.height,'stride':r.stride,'sizeimage':r.sizeimage,
                               'fourcc':r.fourcc,'selected_frame_indices':selected[name]}
                         for name,r in records.items()},'files':files}
    write_json(output/'manifest.json',manifest)
    (output/'README.txt').write_text(
        'Open the PNG files in either camera folder to view individual frames.\n'
        'PNGs are full native resolution, with no resize or brightness normalization.\n'
        'Mira220 PNG: lossless original 8-bit monochrome pixels.\n'
        'IMX296 PNG: 8-bit preview (native 10-bit pixels shifted right by 2).\n'
        'IMX296 native10bit.tiff: original values 0..1023 stored in 16-bit grayscale.\n'
        'These TIFFs may look dark in viewers using the full 0..65535 range.\n'
        'RAW files are exact source frame bytes, including row padding.\n'
        'Raw byte offset = zero-based frame index * sizeimage from manifest.json.\n'
        'mapping.tsv relates slow-video output frames to camera indices and timestamps.\n'
        'The interval includes the frame at exactly 7.0 s. IMX296 uses nearest timestamps.\n'
        'During repeated camera images the plot advances, so select camera frames using\n'
        'the raw image timestamps, not the advancing analyzer cursor.\n')
    (output/'status').write_text('VERIFIED_RAW_FRAME_EXPORT\n')
    print(json.dumps({'output':str(output),'selected':selected,'files':len(files)},indent=2))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for flag in ('run','slow-dir','output'):
        parser.add_argument('--'+flag,type=Path,required=True)
    parser.add_argument('--start',type=Fraction,default=Fraction(6))
    parser.add_argument('--end',type=Fraction,default=Fraction(7))
    args=parser.parse_args()
    extract(args.run,args.slow_dir,args.output,args.start,args.end)

if __name__=='__main__':
    main()
