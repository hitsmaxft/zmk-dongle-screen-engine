#!/usr/bin/env python3
"""Report comparable firmware-memory and RGB565 SPI budget estimates."""
import argparse
import json
import math
import re
from pathlib import Path


def map_symbol(path: Path, name: str) -> int:
    text = path.read_text(errors="replace")
    match = re.search(rf"0x([0-9a-fA-F]+)\s+{re.escape(name)}(?:\s|=)", text)
    if not match:
        raise ValueError(f"{name} not found in {path}")
    return int(match.group(1), 16)


def delta(current, baseline, key):
    before = baseline.get(key) if baseline else None
    after = current.get(key)
    return None if before is None or after is None else after - before


def limit(value, maximum):
    if not maximum:
        return {"maximum": None, "status": "not_configured"}
    if value is None:
        return {"maximum": maximum, "status": "not_evaluated"}
    return {"maximum": maximum, "status": "pass" if value <= maximum else "fail"}


def main(args):
    manifest = json.loads(args.preview_manifest.read_text())
    artifact_manifest = manifest
    artifact_root = args.preview_manifest.parent
    if "native_library" not in artifact_manifest:
        default = manifest.get("default_variant")
        profile_manifest = artifact_root / "profiles" / str(default) / "preview-manifest.json"
        artifact_manifest = json.loads(profile_manifest.read_text())
        artifact_root = profile_manifest.parent
    native = artifact_root / artifact_manifest["native_library"]
    wasm = artifact_root / "theme.wasm"
    full_pixels = args.width * args.height
    dirty_pixels = math.ceil(full_pixels * args.dirty_percent / 100.0)
    spi_bytes = dirty_pixels * 2
    bytes_per_second = spi_bytes * args.fps
    transfer_ms = spi_bytes * 8 / (args.spi_mhz * 1_000_000) * 1000
    frame_ms = 1000 / args.fps

    report = {
        "schema_version": 1,
        "theme": manifest.get("theme"),
        "variant": manifest.get("variant"),
        "inputs": {
            "width": args.width,
            "height": args.height,
            "fps": args.fps,
            "spi_mhz": args.spi_mhz,
            "dirty_percent": args.dirty_percent,
            "strip_pixels": args.strip_pixels,
        },
        "firmware": {
            "flash_bytes": None,
            "ram_bytes": None,
            "map": str(args.firmware_map) if args.firmware_map else None,
        },
        "preview_artifacts": {
            "wasm_bytes": wasm.stat().st_size,
            "native_bytes": native.stat().st_size,
        },
        "ram_model": {
            "strip_buffer_bytes": min(args.strip_pixels, full_pixels) * 2,
            "full_framebuffer_bytes": full_pixels * 2,
        },
        "spi_model": {
            "estimated_dirty_pixels_per_frame": dirty_pixels,
            "estimated_bytes_per_frame": spi_bytes,
            "estimated_bytes_per_second": bytes_per_second,
            "transfer_ms_per_frame": transfer_ms,
            "frame_budget_ms": frame_ms,
            "bus_utilization_percent": transfer_ms / frame_ms * 100,
        },
    }
    if args.firmware_map:
        report["firmware"]["flash_bytes"] = map_symbol(args.firmware_map, "_flash_used")
        report["firmware"]["ram_bytes"] = map_symbol(args.firmware_map, "_image_ram_size")

    baseline = json.loads(args.baseline.read_text()) if args.baseline else None
    if baseline:
        for key, value in report["inputs"].items():
            if baseline.get("inputs", {}).get(key) != value:
                raise ValueError(
                    f"baseline input {key}={baseline.get('inputs', {}).get(key)!r} "
                    f"does not match current {value!r}"
                )
    flat = {
        "flash_bytes": report["firmware"]["flash_bytes"],
        "ram_bytes": report["firmware"]["ram_bytes"],
        "spi_bytes_per_second": bytes_per_second,
        "wasm_bytes": report["preview_artifacts"]["wasm_bytes"],
    }
    baseline_flat = None
    if baseline:
        baseline_flat = {
            "flash_bytes": baseline["firmware"]["flash_bytes"],
            "ram_bytes": baseline["firmware"]["ram_bytes"],
            "spi_bytes_per_second": baseline["spi_model"]["estimated_bytes_per_second"],
            "wasm_bytes": baseline["preview_artifacts"]["wasm_bytes"],
        }
    report["delta"] = {key: delta(flat, baseline_flat, key) for key in flat}
    report["limits"] = {
        "flash_bytes": limit(flat["flash_bytes"], args.max_flash_bytes),
        "ram_bytes": limit(flat["ram_bytes"], args.max_ram_bytes),
        "spi_utilization_percent": limit(
            report["spi_model"]["bus_utilization_percent"],
            args.max_spi_utilization_percent,
        ),
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    unavailable = "n/a"
    lines = [
        f"## ZDSE budget: {report['theme']} / {report['variant']}",
        "",
        "| Metric | Value | Delta | Limit status |",
        "| --- | ---: | ---: | --- |",
    ]
    rows = [
        ("Firmware Flash", flat["flash_bytes"], report["delta"]["flash_bytes"], "flash_bytes", "B"),
        ("Firmware RAM", flat["ram_bytes"], report["delta"]["ram_bytes"], "ram_bytes", "B"),
        ("Preview WASM", flat["wasm_bytes"], report["delta"]["wasm_bytes"], None, "B"),
        ("SPI payload", bytes_per_second, report["delta"]["spi_bytes_per_second"], None, "B/s"),
    ]
    for label, value, change, key, unit in rows:
        shown = unavailable if value is None else f"{value:,.0f} {unit}"
        changed = unavailable if change is None else f"{change:+,.0f} {unit}"
        status = report["limits"][key]["status"] if key else "estimate"
        lines.append(f"| {label} | {shown} | {changed} | {status} |")
    spi = report["spi_model"]
    lines += [
        "",
        f"SPI estimate: {spi['estimated_bytes_per_frame']:,} B/frame, "
        f"{spi['transfer_ms_per_frame']:.3f} ms at {args.spi_mhz:g} MHz, "
        f"{spi['bus_utilization_percent']:.2f}% of a {args.fps:g} FPS frame budget.",
        "",
        f"RAM model: {report['ram_model']['strip_buffer_bytes']:,} B strip versus "
        f"{report['ram_model']['full_framebuffer_bytes']:,} B full framebuffer.",
    ]
    args.markdown.write_text("\n".join(lines) + "\n")
    print(args.output)

    failures = [name for name, value in report["limits"].items() if value["status"] == "fail"]
    if failures:
        raise SystemExit("Budget limit exceeded: " + ", ".join(failures))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview-manifest", type=Path, required=True)
    parser.add_argument("--firmware-map", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--markdown", type=Path, required=True)
    parser.add_argument("--width", type=int, default=280)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--fps", type=float, default=24)
    parser.add_argument("--spi-mhz", type=float, default=32)
    parser.add_argument("--dirty-percent", type=float, default=20)
    parser.add_argument("--strip-pixels", type=int, default=4480)
    parser.add_argument("--max-flash-bytes", type=int, default=0)
    parser.add_argument("--max-ram-bytes", type=int, default=0)
    parser.add_argument("--max-spi-utilization-percent", type=float, default=85)
    parsed = parser.parse_args()
    if (parsed.width <= 0 or parsed.height <= 0 or parsed.fps <= 0 or
            parsed.spi_mhz <= 0 or parsed.strip_pixels <= 0):
        parser.error("dimensions, fps, SPI clock and strip pixels must be positive")
    if parsed.dirty_percent < 0 or parsed.dirty_percent > 100:
        parser.error("dirty percent must be in 0..100")
    main(parsed)
