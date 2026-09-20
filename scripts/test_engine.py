#!/usr/bin/env python3
"""Compile and run the allocation-free C ABI, touch, raster and transport tests."""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main(lvgl: Path) -> None:
    engine = Path(__file__).resolve().parents[1]
    clang = shutil.which("clang")
    if not clang:
        raise RuntimeError("clang is required")
    with tempfile.TemporaryDirectory(prefix="zdse-engine-tests-") as raw:
        output = Path(raw)
        subprocess.run(
            [sys.executable, str(engine / "scripts/generate_raster_assets.py"),
             "--lvgl", str(lvgl.resolve()), "--output",
             str(output / "dongle_raster_assets.h")], check=True)
        common = [clang, "-std=c11", "-Wall", "-Wextra", "-Werror",
                  "-ffp-contract=off", "-I", str(engine / "include"),
                  "-I", str(output)]
        tre = ["src/tre/surface.c", "src/tre/render.c", "src/tre/image.c",
               "src/tre/damage.c", "src/tre/tile.c"]
        suites = {
            "tre-core": ["tests/tre_core.c", *tre],
            "tre-compat": ["tests/tre_compat.c", "src/raster.c", "src/ui.c", *tre],
            "api-v1-3": ["tests/api_v1_2.c", "src/engine.c", "src/raster.c", "src/ui.c", *tre],
            "touch": ["tests/touch.c", "src/engine.c", "src/raster.c", "src/ui.c", *tre],
            "density": ["tests/density.c", "src/raster.c", *tre],
            "transport": ["tests/transport.c"],
        }
        for name, sources in suites.items():
            binary = output / name
            subprocess.run([*common, *[str(engine / source) for source in sources],
                            "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print(f"{name}: PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--lvgl", type=Path, required=True)
    args = parser.parse_args()
    main(args.lvgl)
