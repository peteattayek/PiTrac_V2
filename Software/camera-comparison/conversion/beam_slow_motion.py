# SPDX-License-Identifier: GPL-3.0-or-later
"""Slow raw-camera playback with a waveform clock advancing every output frame."""
from __future__ import annotations
import argparse
from bisect import bisect_left
import csv
from fractions import Fraction
import json
import math
from pathlib import Path
import subprocess
import sys

import av
from PIL import Image, ImageDraw

from .beam_composite import Layout, Trace, native_gray
from .convert import ConversionError, load_run, sha256, write_json
from .deflicker import IlluminationCorrection


def render(run: Path, cache: Path, reference: Path, output: Path,
           begin: float = 13, end: float = 14.5, real_fps: int = 10,
           output_fps: int = 30, window: float = 0.1, mira_first: bool = False,
           deflicker_model: Path | None = None) -> dict:
    if (not math.isfinite(begin) or not math.isfinite(end) or not 0 <= begin < end
            or not 1 <= real_fps <= output_fps <= 120 or output_fps % real_fps):
        raise ConversionError('Invalid clip interval or repetition rates')
    if not 0.02 <= window <= 5:
        raise ConversionError('Invalid detail window')
    ref = json.loads(reference.read_text())
    records = {r.name: r for r in load_run(run)}
    mira = records['mira220']
    correction = IlluminationCorrection(deflicker_model, mira) if deflicker_model is not None else None
    trace = Trace(cache)
    origin = int(ref['reference_origin_us'])
    if 'common_start_offset_us' in ref:
        video_origin = origin + int(ref['common_start_offset_us'])
    elif (ref.get('panels') == ['mira220', 'scrolling_detail', 'full_video_overview']
          and origin == mira.timestamps_us[0]):
        video_origin = origin
    else:
        raise ConversionError('Reference does not identify a supported original video origin')
    offset = float(ref['offset_seconds'])
    if not math.isfinite(offset):
        raise ConversionError('Invalid reference analyzer offset')
    first_us, end_us = video_origin+round(begin*1e6), video_origin+round(end*1e6)
    if first_us < mira.timestamps_us[0] or end_us > mira.timestamps_us[-1]:
        raise ConversionError('Clip outside raw capture')
    indices = tuple(range(bisect_left(mira.timestamps_us, first_us), bisect_left(mira.timestamps_us, end_us)))
    if len(indices) < 2:
        raise ConversionError('Clip has fewer than two real frames')
    analyzer_range = ((first_us-origin)/1e6+offset, (end_us-origin)/1e6+offset)
    if analyzer_range[0]-window < trace.start or analyzer_range[1] > trace.end:
        raise ConversionError('Trace does not cover selected clip and history window')
    layout = Layout(records, 1280, window, analyzer_range, True, True, 'right', begin, mira_first)
    repeats = output_fps//real_fps
    count = len(indices)*repeats
    output.mkdir(parents=False, exist_ok=False)
    (output/'status').write_text('INCOMPLETE\n')
    temporary = output/'beam-slow-motion.incomplete.mp4'
    mapping = output/'frame-map.tsv'
    initial_stat = mira.raw.stat()
    raw_hashes = []
    with mira.raw.open('rb') as raw, mapping.open('x', newline='') as table:
        writer = csv.writer(table, delimiter='\t')
        writer.writerow(['output_frame','output_time_s','mira220_frame','repeat_index',
                         'raw_original_video_time_s','plot_original_video_time_s',
                         'analyzer_time_s','window_start_analyzer_s','window_end_analyzer_s'])
        with av.open(str(temporary), 'w', options={'video_track_timescale':'30000','movflags':'+faststart'}) as container:
            stream = container.add_stream('libx264', rate=output_fps)
            stream.width, stream.height, stream.pix_fmt = layout.width, layout.height, 'yuv420p'
            stream.time_base = stream.codec_context.time_base = Fraction(1,output_fps)
            stream.codec_context.max_b_frames = 0
            stream.codec_context.thread_count = 3
            stream.options = {'crf':'18','preset':'veryfast'}
            for position, source_index in enumerate(indices):
                timestamp = mira.timestamps_us[source_index]
                next_time = min(mira.timestamps_us[source_index+1], end_us)
                raw.seek(source_index*mira.sizeimage)
                data = raw.read(mira.sizeimage)
                import hashlib
                raw_hashes.append(hashlib.sha256(data).hexdigest())
                with native_gray(mira,data) as native:
                    if correction is not None:
                        correction.apply(native,source_index)
                    camera = native.resize((layout.width,layout.bottom_height),Image.Resampling.LANCZOS)
                for repetition in range(repeats):
                    frame_number = position*repeats+repetition
                    # Real analyzer measurements advance between held raw-camera frames.
                    plot_timestamp = timestamp+Fraction(repetition,repeats)*(next_time-timestamp)
                    reference_seconds = float((plot_timestamp-origin)/1_000_000)
                    analyzer = reference_seconds+offset
                    original_video_time = float((plot_timestamp-video_origin)/1_000_000)
                    canvas = Image.new('RGB',(layout.width,layout.height),'#14202d')
                    canvas.paste(layout.overview(trace,analyzer),(0,layout.overview_y))
                    canvas.paste(layout.plot(trace,analyzer,reference_seconds),(0,layout.plot_y))
                    canvas.paste(camera,(0,layout.bottom_y+layout.header))
                    draw = ImageDraw.Draw(canvas)
                    draw.text((14,layout.bottom_y+10),
                              f'MIRA220  |  raw frame {source_index}  |  original video {(timestamp-video_origin)/1e6:.6f} s'
                              f'  |  {real_fps} raw frames/s, {output_fps} fps output'
                              + ('  |  deflickered' if correction is not None else ''),
                              font=layout.font,fill='#f5f9ff')
                    frame = av.VideoFrame.from_image(canvas)
                    frame.pts,frame.time_base = frame_number,Fraction(1,output_fps)
                    for packet in stream.encode(frame):
                        container.mux(packet)
                    writer.writerow([frame_number,f'{frame_number/output_fps:.9f}',source_index,repetition,
                                     f'{(timestamp-video_origin)/1e6:.9f}',f'{original_video_time:.9f}',
                                     f'{analyzer:.9f}',f'{analyzer-window:.9f}',f'{analyzer:.9f}'])
                    if frame_number == 0:
                        canvas.save(output/'layout-first-frame.jpg',quality=95)
                    if source_index == 1229:
                        canvas.save(output/f'crossing-repeat-{repetition}.jpg',quality=95)
                if position % 30 == 0:
                    print(f'Slow motion {position+1}/{len(indices)} raw frames',flush=True)
            for packet in stream.encode(None):
                container.mux(packet)
    report = json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0',
        '-show_streams','-show_packets','-show_entries',
        'stream=width,height,avg_frame_rate,time_base,nb_frames,duration:packet=pts,duration','-of','json',str(temporary)]))
    info,packets = report['streams'][0],report['packets']
    tb = Fraction(info['time_base'])
    if (Fraction(info['avg_frame_rate']) != output_fps or len(packets) != count
        or int(info['nb_frames']) != count or (info['width'],info['height']) != (layout.width,layout.height)):
        raise ConversionError('Encoded stream verification failed')
    for i,packet in enumerate(packets):
        if int(packet['pts'])*tb != Fraction(i,output_fps) or int(packet['duration'])*tb != Fraction(1,output_fps):
            raise ConversionError('Output cadence verification failed')
    if abs(float(info['duration'])-count/output_fps) > 1e-6:
        raise ConversionError('Output duration changed')
    current_stat = mira.raw.stat()
    if (initial_stat.st_size,initial_stat.st_mtime_ns) != (current_stat.st_size,current_stat.st_mtime_ns):
        raise ConversionError('Raw recording changed during rendering')
    final = output/f'beam-slow-motion-{output_fps}fps.mp4'
    temporary.rename(final)
    source_fps = (len(indices)-1)*1e6/(mira.timestamps_us[indices[-1]]-mira.timestamps_us[indices[0]])
    metadata = {
        'file':final.name,'source_run':str(run),'reference_composite':str(reference),
        'original_video_interval_seconds':[begin,end],'source_frame_indices':list(indices),
        'source_frame_sha256':raw_hashes,'raw_frames':len(indices),'raw_playback_fps':real_fps,
        'output_fps':output_fps,'repeats_per_raw_frame':repeats,'output_frames':count,
        'duration_seconds':count/output_fps,
        'camera_interpolation':('none; each consecutive raw frame appears once' if repeats == 1 else
                                f'none; each raw image held for {repeats} output frames'),
        'plot_update_fps':output_fps,
        'plot_clock':('selected native raw timestamp, exactly matching the displayed camera frame' if repeats == 1 else
                      f'raw timestamp plus repeat_index/{repeats} of interval to next raw timestamp; final interval clipped at selection end'),
        'waveform_data':'recorded CSV min/max envelope, recomputed every output frame; no synthetic analog samples',
        'plot_window_seconds':window,'window_position':'right','detail_cursor_visible':False,
        'overview_analyzer_range_seconds':analyzer_range,'overview_axis':'original video seconds',
        'reference_origin_us':origin,'original_video_origin_us':video_origin,'analyzer_offset_seconds':offset,
        'alignment':('saved approximate event-based alignment; both plots use the displayed native frame timestamp' if repeats == 1 else
                     'saved approximate event-based alignment; during repeated images the plot advances beyond the held camera timestamp'),
        'panels':(['mira220','scrolling_detail','full_video_overview'] if mira_first else
                  ['full_video_overview','scrolling_detail','mira220']),
        'frame_index_convention':'zero-based','source_fps':source_fps,'slowdown_factor':source_fps/real_fps,
        'pixel_mapping':('native gray8 plus display-only additive illumination correction; aspect-preserving viewing resize'
                         if correction is not None else
                         'native linear gray8, aspect-preserving viewing resize; no brightness gain or normalization'),
        'deflicker':correction.metadata if correction is not None else None,
        'source_raw_bytes':initial_stat.st_size,'source_raw_mtime_ns':initial_stat.st_mtime_ns,
        'reference_sha256':sha256(reference),'trace_cache_sha256':sha256(cache),
        'output_width':layout.width,'output_height':layout.height,'output_sha256':sha256(final),
        'frame_map_sha256':sha256(mapping),'trace':trace.metadata,
    }
    write_json(output/'composite.json',metadata)
    (output/'status').write_text('VERIFIED_SLOW_MOTION\n')
    print(json.dumps({k:metadata[k] for k in ('raw_frames','output_frames','output_fps','duration_seconds','output_sha256')},indent=2),flush=True)
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('run','cache','reference','output'):
        parser.add_argument('--'+flag,type=Path,required=True)
    parser.add_argument('--begin',type=float,default=13)
    parser.add_argument('--end',type=float,default=14.5)
    parser.add_argument('--raw-playback-fps',type=int,default=10,
                        help='Consecutive native camera frames shown per playback second')
    parser.add_argument('--output-fps',type=int,default=30,
                        help='Set equal to raw-playback-fps to show each native frame exactly once')
    parser.add_argument('--window-seconds',type=float,default=0.1)
    parser.add_argument('--mira-first',action='store_true',
                        help='Put Mira220 above the detail plot and full-clip overview')
    parser.add_argument('--deflicker-model',type=Path,
                        help='Optional recording-specific display correction; raw files and trace data stay unchanged')
    args = parser.parse_args()
    try:
        render(args.run,args.cache,args.reference,args.output,args.begin,args.end,
               args.raw_playback_fps,args.output_fps,args.window_seconds,args.mira_first,args.deflicker_model)
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, av.error.FFmpegError) as error:
        print(f'ERROR: {error}',file=sys.stderr)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
