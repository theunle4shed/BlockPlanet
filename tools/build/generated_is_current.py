"""Whether the game code generated from the ROM is still current.

Exits 0 when the manifest's committed inputs, the recompiler and every
generated file still match what the manifest recorded, so the code need not be
generated again (the slow part of an update); 1 otherwise.

    python tools/build/generated_is_current.py MANIFEST GENERATED_DIR RECOMPILER
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def current(manifest_path: Path, generated: Path, recompiler: Path) -> str:
    """An empty string when current, else the reason it is not."""
    if not manifest_path.is_file():
        return "no manifest yet"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for item in manifest.get("committed_inputs", []):
        path = ROOT / item["path"]
        if not path.is_file() or sha256(path) != item["sha256"]:
            return f"{item['path']} changed"
    expected = manifest.get("recompiler", {}).get("executable_sha256")
    if not recompiler.is_file() or sha256(recompiler) != expected:
        return "the recompiler changed"
    for item in manifest.get("generated_files", []):
        path = generated / item["path"]
        if not path.is_file() or sha256(path) != item["sha256"]:
            return f"generated {item['path']} is missing or changed"
    return ""


if __name__ == "__main__":
    reason = current(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]))
    print(reason or "the generated game code is current")
    sys.exit(1 if reason else 0)
