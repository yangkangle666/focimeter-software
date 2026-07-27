"""Generate deterministic grayscale Hartmann reference and measurement fixtures."""

from pathlib import Path
import struct
import zlib


WIDTH = 640
HEIGHT = 480
GRID_SPACING = 28
CLIP_RADIUS = 205
BACKGROUND = 8
SPOT_WEIGHTS = (7, 25, 69, 145, 255, 145, 69, 25, 7)
SPOT_KERNEL = tuple(
    tuple((239 * x_weight * y_weight + 32_512) // 65_025 for x_weight in SPOT_WEIGHTS)
    for y_weight in SPOT_WEIGHTS
)
OUTPUT_DIR = Path(__file__).resolve().parents[1] / "data/synthetic/generated_images"


def displaced_point(x: float, y: float, cx: float, cy: float) -> tuple[float, float]:
    dx = x - cx
    dy = y - cy
    return x + 0.006 * dx + 0.003 * dy, y - 0.002 * dx + 0.004 * dy


def grid_points() -> list[tuple[float, float]]:
    cx = (WIDTH - 1) / 2
    cy = (HEIGHT - 1) / 2
    extent = CLIP_RADIUS // GRID_SPACING + 1
    return [
        (cx + column * GRID_SPACING, cy + row * GRID_SPACING)
        for row in range(-extent, extent + 1)
        for column in range(-extent, extent + 1)
        if (column * GRID_SPACING) ** 2 + (row * GRID_SPACING) ** 2 <= CLIP_RADIUS ** 2
    ]


def render(points: list[tuple[float, float]]) -> bytes:
    pixels = bytearray([BACKGROUND]) * (WIDTH * HEIGHT)
    kernel_radius = len(SPOT_KERNEL) // 2
    for x, y in points:
        center_x = round(x)
        center_y = round(y)
        for kernel_y, kernel_row in enumerate(SPOT_KERNEL):
            pixel_y = center_y + kernel_y - kernel_radius
            for kernel_x, intensity in enumerate(kernel_row):
                pixel_x = center_x + kernel_x - kernel_radius
                index = pixel_y * WIDTH + pixel_x
                pixels[index] = max(pixels[index], min(255, BACKGROUND + intensity))
    return bytes(pixels)


def png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    checksum = zlib.crc32(chunk_type + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", checksum)


def encode_grayscale_png(pixels: bytes) -> bytes:
    scanlines = b"".join(
        b"\x00" + pixels[row * WIDTH:(row + 1) * WIDTH]
        for row in range(HEIGHT)
    )
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 0, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + png_chunk(b"IEND", b"")
    )


def main() -> int:
    reference_points = grid_points()
    cx = (WIDTH - 1) / 2
    cy = (HEIGHT - 1) / 2
    measurement_points = [displaced_point(x, y, cx, cy) for x, y in reference_points]
    fixtures = {
        "hartmann_reference.png": reference_points,
        "hartmann_measurement.png": measurement_points,
    }

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for filename, points in fixtures.items():
        output_path = OUTPUT_DIR / filename
        output_path.write_bytes(encode_grayscale_png(render(points)))
        print(f"wrote {output_path} ({WIDTH}x{HEIGHT}, {len(points)} spots)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
