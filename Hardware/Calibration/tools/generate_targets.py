"""Generate physical-size ChArUco targets from OpenCV's actual board layout."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import cv2
import numpy as np
from numpy.typing import NDArray
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas

ROOT = Path(__file__).resolve().parents[1]
BOARD_ID = "pitrac-charuco-11x8-20mm-v1"
PAPERS = {"a4": (297.0, 210.0), "letter": (279.4, 215.9)}
PIXELS_PER_MM = 14


def make_board() -> cv2.aruco.CharucoBoard:
    if not cv2.__version__.startswith("4.13."):
        raise RuntimeError("This layout is bound to OpenCV 4.13.x; use requirements.txt.")
    board = cv2.aruco.CharucoBoard(
        (11, 8), 20.0, 15.0,
        cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_5X5_250),
    )
    board.setLegacyPattern(False)
    return board


def definition() -> dict[str, Any]:
    board = make_board()
    markers = []
    for identifier, corners in zip(board.getIds(), board.getObjPoints()):
        cells = cv2.aruco.generateImageMarker(
            board.getDictionary(), int(identifier), 7, borderBits=1,
        )
        markers.append({
            "id": int(identifier),
            "corners_mm": corners.tolist(),
            "black_cells": (cells == 0).astype(int).tolist(),
        })
    return {
        "schema": "pitrac-charuco-target-v1",
        "board_id": BOARD_ID,
        "dictionary": "DICT_5X5_250",
        "squares_x": 11, "squares_y": 8,
        "square_length_mm": 20.0, "marker_length_mm": 15.0,
        "marker_border_bits": 1, "legacy_pattern": False,
        "pattern_size_mm": [220.0, 160.0],
        "minimum_white_border_mm": 15.0,
        "coordinate_frame": "Top-left of pattern; X right, Y down, Z=0; units mm.",
        "opencv_version": cv2.__version__,
        "dictionary_bytes_sha256": hashlib.sha256(board.getDictionary().bytesList.tobytes()).hexdigest(),
        "markers": markers,
        "chessboard_corners": [
            {"id": index, "position_mm": corner.tolist()}
            for index, corner in enumerate(board.getChessboardCorners())
        ],
        "papers_mm": {name: list(size) for name, size in PAPERS.items()},
        "digital_validation_is_not_physical_print_or_850nm_qualification": True,
    }


def black_rectangles(board: dict[str, Any]) -> list[tuple[float, float, float, float]]:
    white = {
        (int(marker["corners_mm"][0][0] // 20), int(marker["corners_mm"][0][1] // 20))
        for marker in board["markers"]
    }
    rectangles = [
        (float(x * 20), float(y * 20), 20.0, 20.0)
        for y in range(8) for x in range(11) if (x, y) not in white
    ]
    for marker in board["markers"]:
        x, y, _ = marker["corners_mm"][0]
        for row, cells in enumerate(marker["black_cells"]):
            for col, black in enumerate(cells):
                if black:
                    rectangles.append((x + col * 15 / 7, y + row * 15 / 7, 15 / 7, 15 / 7))
    return rectangles


def raster(board: dict[str, Any]) -> NDArray[np.uint8]:
    image = np.full((160 * PIXELS_PER_MM, 220 * PIXELS_PER_MM), 255, np.uint8)
    for x, y, width, height in black_rectangles(board):
        left, top, right, bottom = np.rint(
            np.array([x, y, x + width, y + height]) * PIXELS_PER_MM,
        ).astype(int)
        image[top:bottom, left:right] = 0
    return image


def page_geometry(paper: str) -> dict[str, float]:
    width, height = PAPERS[paper]
    ox, oy = (width - 220) / 2, (height - 160) / 2
    return {
        "width": width, "height": height, "ox": ox, "oy": oy,
        "horizontal_x": width / 2 - 50, "horizontal_y": oy + 160 + 16,
        "vertical_x": ox - 20, "vertical_y": height / 2 - 50,
    }


def svg_document(board: dict[str, Any], paper: str) -> str:
    g = page_geometry(paper)
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{g["width"]}mm" '
        f'height="{g["height"]}mm" viewBox="0 0 {g["width"]} {g["height"]}">',
        f'<title>{BOARD_ID}: {paper.upper()} landscape</title>',
        '<desc>Print at actual size, no fit/shrink, no headers. Verify both 100 mm bars. '
        'Digital pattern only: physical flatness and 850 nm contrast are unqualified.</desc>',
        f'<rect width="{g["width"]}" height="{g["height"]}" fill="white"/>',
        f'<g data-role="pattern" transform="translate({g["ox"]} {g["oy"]})" fill="black">',
    ]
    lines += [
        f'<rect x="{x:.8f}" y="{y:.8f}" width="{w:.8f}" height="{h:.8f}"/>'
        for x, y, w, h in black_rectangles(board)
    ]
    lines += [
        "</g>",
        '<g fill="black" font-family="Arial,Helvetica,sans-serif">',
        f'<text x="8" y="8" font-size="3">{BOARD_ID} | 20 mm squares | 15 mm markers | PRINT 100%</text>',
        "</g>",
        '<g data-role="scale-checks" fill="none" stroke="black" stroke-width="0.15">',
        f'<line data-scale="x" x1="{g["horizontal_x"]}" y1="{g["horizontal_y"]}" '
        f'x2="{g["horizontal_x"]+100}" y2="{g["horizontal_y"]}"/>',
        f'<line data-scale="y" x1="{g["vertical_x"]}" y1="{g["vertical_y"]}" '
        f'x2="{g["vertical_x"]}" y2="{g["vertical_y"]+100}"/>',
    ]
    for x in (g["horizontal_x"], g["horizontal_x"] + 100):
        lines.append(f'<line x1="{x}" y1="{g["horizontal_y"]-1}" x2="{x}" y2="{g["horizontal_y"]+1}"/>')
    for y in (g["vertical_y"], g["vertical_y"] + 100):
        lines.append(f'<line x1="{g["vertical_x"]-1}" y1="{y}" x2="{g["vertical_x"]+1}" y2="{y}"/>')
    lines += [
        "</g>",
        f'<text x="{g["horizontal_x"]-3}" y="{g["horizontal_y"]+1.2}" '
        'text-anchor="end" font-size="2.2" font-family="Arial">100 mm X</text>',
        f'<text transform="translate({g["vertical_x"]-2} {g["height"]/2}) rotate(-90)" '
        'text-anchor="middle" font-size="2.2" font-family="Arial">100 mm Y</text>',
        "</svg>",
    ]
    return "\n".join(lines) + "\n"


def write_pdf(board: dict[str, Any], paper: str, path: Path) -> None:
    g = page_geometry(paper)
    pdf = canvas.Canvas(
        str(path), pagesize=(g["width"] * mm, g["height"] * mm),
        pageCompression=1, invariant=1,
    )
    pdf.setTitle(f"{BOARD_ID}: {paper.upper()} landscape")
    pdf.setAuthor("PiTrac calibration tooling")
    pdf.setFillColorRGB(1, 1, 1)
    pdf.rect(0, 0, g["width"] * mm, g["height"] * mm, fill=1, stroke=0)
    pdf.setFillColorRGB(0, 0, 0)
    for x, y, width, height in black_rectangles(board):
        pdf.rect((g["ox"] + x) * mm, (g["height"] - g["oy"] - y - height) * mm,
                 width * mm, height * mm, fill=1, stroke=0)
    pdf.setFont("Helvetica", 3 * mm)
    pdf.drawString(8 * mm, (g["height"] - 8) * mm,
                   f"{BOARD_ID} | 20 mm squares | 15 mm markers | PRINT 100%")
    pdf.setLineWidth(.15 * mm)
    hx, hy = g["horizontal_x"], g["horizontal_y"]
    vx, vy = g["vertical_x"], g["vertical_y"]
    pdf.line(hx * mm, (g["height"] - hy) * mm, (hx + 100) * mm, (g["height"] - hy) * mm)
    pdf.line(vx * mm, (g["height"] - vy) * mm, vx * mm, (g["height"] - vy - 100) * mm)
    for x in (hx, hx + 100):
        pdf.line(x * mm, (g["height"] - hy - 1) * mm, x * mm, (g["height"] - hy + 1) * mm)
    for y in (vy, vy + 100):
        pdf.line((vx - 1) * mm, (g["height"] - y) * mm, (vx + 1) * mm, (g["height"] - y) * mm)
    pdf.setFont("Helvetica", 2.2 * mm)
    pdf.drawRightString((hx - 3) * mm, (g["height"] - hy - 1.2) * mm, "100 mm X")
    pdf.saveState()
    pdf.translate((vx - 2) * mm, g["height"] / 2 * mm)
    pdf.rotate(90)
    pdf.drawCentredString(0, 0, "100 mm Y")
    pdf.restoreState()
    pdf.showPage()
    pdf.save()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "targets")
    args = parser.parse_args()
    cv2.setNumThreads(1)
    board = definition()
    reference = make_board().generateImage((3080, 2240), marginSize=0, borderBits=1)
    image = raster(board)
    if not np.array_equal(image, reference):
        raise RuntimeError("Vector board differs from OpenCV's reference raster.")
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "board.json").write_text(json.dumps(board, indent=2) + "\n", encoding="utf-8")
    if not cv2.imwrite(str(args.output / "board-raster.png"), image):
        raise OSError("Could not save target raster")
    for paper in PAPERS:
        (args.output / f"charuco-{paper}.svg").write_text(svg_document(board, paper), encoding="utf-8")
        write_pdf(board, paper, args.output / f"charuco-{paper}.pdf")
    print(f"Generated verified board: {args.output}")


if __name__ == "__main__":
    main()
