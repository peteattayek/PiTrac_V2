# Printable target files

Print **[charuco-a4.pdf](charuco-a4.pdf)** or
**[charuco-letter.pdf](charuco-letter.pdf)** at **Actual size / 100%**,
landscape, without fit/shrink, headers, or footers. The matching SVG files
have explicit millimeter page dimensions and the same physical pattern.
Do not resize one paper format to make the other.

The pattern is 220 x 160 mm: 11 x 8 squares, 20 mm pitch, 15 mm markers.
It uses `DICT_5X5_250`, one marker-border bit, and the non-legacy OpenCV
4.13 board layout. Both pages include independent 100 mm X/Y scale bars.
Check the printed bars and square pitches in both directions.

**[board-raster.png](board-raster.png) is a digital detector/reference image,
not a print-size authority.** It does not establish a physical print scale.
Use the physical-size PDF or SVG instead.

[board.json](board.json) records the actual OpenCV marker cells, marker IDs,
70 chessboard corner IDs/positions, millimeter coordinate frame, dictionary
hash, and generating OpenCV version. Generated vector cells are verified
against OpenCV's raster before files are written.

Mount the printed target flat on rigid matte backing. Verify its actual
contrast under the final 850 nm filter/illumination conditions. Digital
detection, a PDF page size, or an SVG dimension does not establish print
accuracy, physical flatness, infrared opacity, or camera calibration.

Regenerate through `../tools/generate_targets.py`; see
[the package entry guide](../README.md) for the environment and commands.
