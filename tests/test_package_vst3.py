from pathlib import Path
from zipfile import ZipFile
import pytest

from scripts.package_vst3 import package


def test_package_plugin_directory(tmp_path):
    bundle = tmp_path / "build" / "subdir" / "Expressive MIDI Bridge.vst3"
    binary = bundle / "Contents" / "x86_64-win" / "Expressive MIDI Bridge.vst3"
    binary.parent.mkdir(parents=True)
    binary.write_bytes(b"plugin")
    output = package(tmp_path / "build", "Windows", tmp_path / "dist")
    assert output.name == "Expressive-MIDI-Bridge-Windows.zip"
    with ZipFile(output) as archive:
        assert archive.namelist() == ["Expressive MIDI Bridge.vst3/Contents/x86_64-win/Expressive MIDI Bridge.vst3",
                                      "README.txt"]
        assert archive.read(archive.namelist()[0]) == b"plugin"
        text = archive.read("README.txt").decode("utf-8-sig")
        assert "Reset Blocklist" in text and "\r\n" in text


def test_package_plugin_file(tmp_path):
    bundle = tmp_path / "build" / "MyPlugin.vst3"
    bundle.parent.mkdir()
    bundle.write_bytes(b"plugin")
    output = package(tmp_path / "build", "Windows", tmp_path / "dist", readme=None)
    with ZipFile(output) as archive:
        assert archive.namelist() == ["MyPlugin.vst3"]


def test_package_no_plugin_fails(tmp_path):
    build = tmp_path / "build"
    build.mkdir()
    with pytest.raises(RuntimeError, match="exactly one"):
        package(build, "macos", tmp_path / "dist")
