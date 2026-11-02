#!/usr/bin/env python3
"""Exercise user-visible persistent controller configuration."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("test_root")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(dir=args.test_root) as directory:
        data_dir = Path(directory)

        def run(*command):
            return subprocess.run([args.binary, "--data-dir", str(data_dir), *command],
                                  capture_output=True, text=True)

        before = run("show")
        assert before.returncode == 0, before.stderr
        assert json.loads(before.stdout)["family_override"] == "auto"
        assert run("family", "nintendo-switch").returncode == 0
        assert run("bind", "confirm", "east").returncode == 0
        after = run("show")
        assert after.returncode == 0, after.stderr
        settings = json.loads(after.stdout)
        assert settings["family_override"] == "nintendo-switch"
        assert settings["bindings"]["confirm"] == "east"
        assert settings["schema_version"] == 7
        assert settings["bindings"]["magic"] == "west"
        assert settings["graphics"] == {
            "resolution": "auto", "aspect": "16:9", "refresh": "original",
            "window_mode": "windowed", "msaa_samples": 0,
            "presentation_filter": "pixel", "texture_filter": "three-point",
            "sphere": "extended"}
        assert run("bind", "magic", "right-shoulder").returncode == 0
        assert run("graphics", "sphere", "original").returncode == 0
        assert run("graphics", "resolution", "2160p").returncode == 0
        assert run("graphics", "aspect", "16:9").returncode == 0
        assert run("graphics", "refresh", "120").returncode == 0
        assert run("graphics", "window-mode", "fullscreen").returncode == 0
        settings = json.loads(run("show").stdout)
        assert settings["bindings"]["magic"] == "right-shoulder"
        assert settings["graphics"] == {
            "resolution": "2160p", "aspect": "16:9", "refresh": "120",
            "window_mode": "fullscreen", "msaa_samples": 0,
            "presentation_filter": "pixel", "texture_filter": "three-point",
            "sphere": "original"}
        assert run("graphics", "sphere", "full").returncode != 0
        invalid = run("family", "unknown-pad")
        assert invalid.returncode != 0
        assert run("graphics", "resolution", "500p").returncode != 0
        assert run("graphics", "refresh", "unlimited").returncode != 0
        assert run("graphics", "window-mode", "exclusive").returncode != 0
        assert json.loads(run("show").stdout) == settings

        # Profiles: list, add, choose a character, delete and restore.
        profiles = json.loads(run("profiles").stdout)
        assert profiles["active"] == "Player 1" and len(profiles["profiles"]) == 1
        assert run("profile-add", "Sam").returncode == 0
        assert run("profile-add", "sam").returncode != 0
        assert run("profile-character", "Sam", "rescue-2", "3").returncode == 0
        assert run("profile-character", "Sam", "rescue-2", "9").returncode != 0
        assert run("profile-use", "Sam").returncode == 0
        profiles = json.loads(run("profiles").stdout)
        assert profiles["active"] == "Sam"
        assert {"id": "2", "name": "Sam", "character": "rescue-2", "palette": 3} in profiles["profiles"]
        assert run("profile-delete", "Sam").returncode == 0
        profiles = json.loads(run("profiles").stdout)
        assert profiles["active"] == "Player 1" and [d["name"] for d in profiles["deleted"]] == ["Sam"]
        assert run("profile-restore", "Sam").returncode == 0
        profiles = json.loads(run("profiles").stdout)
        assert len(profiles["profiles"]) == 2 and profiles["deleted"] == []
        assert run("profile-delete", "Nobody").returncode != 0
    print("config_cli persistence_and_rejection=pass")


if __name__ == "__main__":
    main()
