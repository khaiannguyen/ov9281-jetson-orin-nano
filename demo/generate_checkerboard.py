#!/usr/bin/env python3
"""
generate_checkerboard.py — Creates a precisely-dimensioned checkerboard
calibration pattern PDF, sized for A3 printing.

Board spec (must match CHESSBOARD_INNER_CORNERS and SQUARE_SIZE_MM in
calibrate_stereo.py exactly, since OpenCV's calibration accuracy depends on
knowing the real physical square size):

    9 x 6 inner corners  (i.e. a 10 x 7 grid of squares)
    25mm per square

IMPORTANT WHEN PRINTING:
  - Print at 100% scale / "Actual size" -- do NOT let the printer driver
    auto-fit-to-page, or the physical square size will no longer match
    SQUARE_SIZE_MM and every calibration measurement will be wrong.
  - A 100mm reference ruler is included on the page specifically so you can
    verify with an actual ruler after printing that scaling was not applied.
  - Mount/tape the print completely flat -- any bulge or curl introduces
    real geometric distortion into every calibration shot.
"""

from reportlab.lib.pagesizes import A3
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas

SQUARE_SIZE_MM = 25
COLS = 10  # squares across
ROWS = 7   # squares down
# -> 9 x 6 *inner corners*, the number cv2.findChessboardCorners expects

OUTPUT_PATH = "checkerboard_A3_9x6_25mm.pdf"


def draw_checkerboard(c: canvas.Canvas, page_w_mm: float, page_h_mm: float):
    board_w = COLS * SQUARE_SIZE_MM
    board_h = ROWS * SQUARE_SIZE_MM

    # Center the board on the page
    x0 = (page_w_mm - board_w) / 2
    y0 = (page_h_mm - board_h) / 2

    for row in range(ROWS):
        for col in range(COLS):
            if (row + col) % 2 == 0:
                x = (x0 + col * SQUARE_SIZE_MM) * mm
                y = (y0 + row * SQUARE_SIZE_MM) * mm
                c.setFillColorRGB(0, 0, 0)
                c.rect(x, y, SQUARE_SIZE_MM * mm, SQUARE_SIZE_MM * mm,
                       stroke=0, fill=1)

    return x0, y0, board_w, board_h


def draw_reference_ruler(c: canvas.Canvas, x0_mm: float, y_mm: float):
    """A 100mm line with tick marks every 10mm, for post-print verification."""
    c.setStrokeColorRGB(0, 0, 0)
    c.setLineWidth(1)
    c.line(x0_mm * mm, y_mm * mm, (x0_mm + 100) * mm, y_mm * mm)
    for i in range(11):
        tick_h = 3 if i % 5 == 0 else 1.5
        x = (x0_mm + i * 10) * mm
        c.line(x, y_mm * mm, x, (y_mm + tick_h) * mm)
    c.setFont("Helvetica", 8)
    c.drawString(x0_mm * mm, (y_mm - 5) * mm,
                 "100mm reference ruler -- verify with a real ruler after printing")


def main():
    page_w_mm, page_h_mm = A3[0] / mm, A3[1] / mm  # A3 landscape-safe values
    c = canvas.Canvas(OUTPUT_PATH, pagesize=A3)

    x0, y0, board_w, board_h = draw_checkerboard(c, page_w_mm, page_h_mm)

    # Reference ruler along the bottom margin
    draw_reference_ruler(c, x0, max(y0 - 20, 10))

    # Labels
    c.setFillColorRGB(0, 0, 0)
    c.setFont("Helvetica-Bold", 12)
    c.drawString(10 * mm, (page_h_mm - 12) * mm,
                 f"Stereo calibration checkerboard -- {COLS}x{ROWS} squares, "
                 f"{SQUARE_SIZE_MM}mm each ({COLS-1}x{ROWS-1} inner corners)")
    c.setFont("Helvetica", 9)
    c.drawString(10 * mm, (page_h_mm - 18) * mm,
                 "PRINT AT 100% / ACTUAL SIZE -- do not scale to fit page. "
                 "Mount perfectly flat.")

    c.showPage()
    c.save()
    print(f"Wrote {OUTPUT_PATH}")
    print(f"Board: {COLS}x{ROWS} squares ({COLS-1}x{ROWS-1} inner corners), "
          f"{SQUARE_SIZE_MM}mm/square, page A3 ({page_w_mm:.0f}x{page_h_mm:.0f}mm)")


if __name__ == "__main__":
    main()
