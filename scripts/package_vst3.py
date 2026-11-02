"""Zip a freshly built VST3 bundle for a GitHub Actions download.

Usage: python scripts/package_vst3.py --build build/vst3 --platform windows-x64
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
import zipfile


def package(build_dir: Path, platform: str, output_dir: Path) -> Path:
    if not build_dir.is_dir():
        raise FileNotFoundError(f"Build directory does not exist: {build_dir}")
    matches = sorted((p for p in build_dir.rglob("*.vst3") if p.is_dir() or p.is_file()), key=str)
    # A .vst3 can contain nested files, but other files inside it must not be
    # interpreted as independent plugins. JUCE creates a single top-level bundle.
    roots = [p for p in matches if not any(a.suffix == ".vst3" for a in p.parents)]
    if len(roots) != 1:
        raise RuntimeError(f"Expected exactly one .vst3 bundle, found {len(roots)}: {roots}")
    root = roots[0]
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"Expressive-MIDI-Player-{platform}.zip"
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        if root.is_file():
            archive.write(root, arcname=root.name)
        else:
            files = sorted(p for p in root.rglob("*") if p.is_file())
            if not files:
                raise RuntimeError(f"Empty VST3 bundle: {root}")
            for f in files:
                archive.write(f, arcname=str(Path(root.name) / f.relative_to(root)))
    print(f"Packaged {root} -> {output}")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    try:
        package(args.build, args.platform, args.output)
    except (RuntimeError, FileNotFoundError) as ex:
        print(f"Error: {ex}", file=sys.stderr)
        raise SystemExit(1) from ex


if __name__ == "__main__":
    main()
