"""Debug/Release builds and production DFU packaging."""

import argparse
import hashlib
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = json.loads((ROOT / "tools.json").read_text(encoding="utf-8"))
CACHE = Path(os.environ.get("CI_CACHE_DIR", ROOT / ".ci-cache")).resolve()
BUILD_ROOT = ROOT / "build" / "ci"
CONFIGURATIONS = ("Debug", "Release")


def fail(message):
    raise RuntimeError(message)


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def require(path, label):
    if not path.is_file() or not path.stat().st_size:
        fail(f"missing {label}: {path}")
    return path


def run(command, **kwargs):
    print(f"[ci] {shlex.join(map(str, command))}", flush=True)
    subprocess.run(list(map(str, command)), check=True, **kwargs)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build and package one SEGGER Embedded Studio configuration."
    )
    parser.add_argument(
        "--configuration",
        choices=CONFIGURATIONS,
        required=True,
        help="SES configuration to build",
    )
    return parser.parse_args()


def source_revision():
    revision = os.environ.get("CI_SOURCE_REVISION") or os.environ.get("GITHUB_SHA")
    if revision:
        return revision
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def source_dirty():
    try:
        status = subprocess.check_output(
            ["git", "status", "--porcelain", "--untracked-files=normal"],
            cwd=ROOT,
            text=True,
        )
        return bool(status.strip())
    except (OSError, subprocess.CalledProcessError):
        return None


def download(tool, *, segger=False):
    destination = CACHE / "downloads" / tool["archive"]
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and digest(destination) == tool["sha256"]:
        return destination
    destination.unlink(missing_ok=True)
    data = None
    if segger:
        data = urllib.parse.urlencode(
            {"accept_license_agreement": "accepted", "submit": "Download software"}
        ).encode()
    request = urllib.request.Request(
        tool["url"], data=data, headers={"User-Agent": "SPI2BLE-CI/1"}
    )
    temporary = destination.with_suffix(destination.suffix + ".part")
    temporary.unlink(missing_ok=True)
    try:
        with (
            urllib.request.urlopen(request) as response,
            temporary.open("wb") as output,
        ):
            shutil.copyfileobj(response, output)
        if digest(temporary) != tool["sha256"]:
            fail(f"checksum mismatch for {tool['archive']}")
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination


def archive_target(base, name):
    target = (base / name).resolve()
    if target != base and base not in target.parents:
        fail(f"archive entry escapes destination: {name}")


def unpack(archive, destination):
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as bundle:
            for member in bundle.infolist():
                archive_target(destination.resolve(), member.filename)
            bundle.extractall(destination)
    else:
        with tarfile.open(archive) as bundle:
            for member in bundle.getmembers():
                archive_target(destination.resolve(), member.name)
            bundle.extractall(destination, filter="data")


def replace(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        shutil.rmtree(path)


def setup_sdk():
    tool = CONFIG["nrf5_sdk"]
    destination = CACHE / f"nrf5-sdk-{tool['version']}"
    if not (destination / ".ci-ready").is_file():
        print(f"[ci] installing nRF5 SDK {tool['version']}", flush=True)
        archive = download(tool)
        with tempfile.TemporaryDirectory(dir=CACHE) as temporary_name:
            temporary = Path(temporary_name)
            unpack(archive, temporary)
            source = temporary / tool["extracted_root"]
            if not source.is_dir():
                fail(f"SDK archive lacks {tool['extracted_root']}")
            replace(destination)
            shutil.move(source, destination)
        archive.unlink(missing_ok=True)
        (destination / ".ci-ready").touch()
    else:
        print(f"[ci] cached nRF5 SDK {tool['version']}", flush=True)

    patch = tool["patch"]
    target = destination / patch["target"]
    text = require(target, "SDK UUID source").read_text(encoding="utf-8")
    before, after = text.count(patch["before"]), text.count(patch["after"])
    if (before, after) == (1, 0):
        target.write_text(
            text.replace(patch["before"], patch["after"]), encoding="utf-8"
        )
    elif (before, after) != (0, 1):
        fail(f"SDK UUID patch precondition failed: before={before}, after={after}")

    workspace = destination / tool["workspace_relative_path"]
    replace(workspace)
    workspace.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(
        ROOT,
        workspace,
        ignore=shutil.ignore_patterns(
            ".git",
            ".ci-cache",
            "build",
            "Output",
            "__pycache__",
            "*.pyc",
        ),
    )
    return workspace


def setup_ses():
    tool = CONFIG["segger_embedded_studio"]
    destination = CACHE / f"segger-embedded-studio-{tool['version']}"
    executable = destination / "bin" / "emBuild"
    if not executable.is_file():
        print(f"[ci] installing SEGGER Embedded Studio {tool['version']}", flush=True)
        archive = download(tool, segger=True)
        with tempfile.TemporaryDirectory(dir=CACHE) as temporary_name:
            temporary = Path(temporary_name)
            unpack(archive, temporary)
            installers = list(temporary.glob("*/install_segger_embedded_studio"))
            if len(installers) != 1:
                fail("SES archive does not contain exactly one installer")
            replace(destination)
            run(
                [
                    installers[0],
                    "--accept-license",
                    "--copy-files-to",
                    destination,
                    "--minimal-install",
                    "--silent",
                ]
            )
        archive.unlink(missing_ok=True)
    else:
        print(f"[ci] cached SEGGER Embedded Studio {tool['version']}", flush=True)
    return require(executable, "emBuild")


def setup_nrf_cli():
    tool = CONFIG["nrf_command_line_tools"]
    destination = CACHE / f"nrf-command-line-tools-{tool['version']}"
    executable = destination / "bin" / "mergehex"
    if not executable.is_file():
        print(f"[ci] installing nRF Command Line Tools {tool['version']}", flush=True)
        archive = download(tool)
        with tempfile.TemporaryDirectory(dir=CACHE) as temporary_name:
            temporary = Path(temporary_name)
            unpack(archive, temporary)
            source = temporary / "nrf-command-line-tools"
            if not source.is_dir():
                fail("nRF CLI archive lacks nrf-command-line-tools")
            replace(destination)
            shutil.move(source, destination)
        archive.unlink(missing_ok=True)
    else:
        print(f"[ci] cached nRF Command Line Tools {tool['version']}", flush=True)
    return destination, require(executable, "mergehex")


def setup_nrfutil():
    tool = CONFIG["nrfutil"]
    destination = CACHE / f"nrfutil-{tool['version']}"
    executable = destination / "bin" / "nrfutil"
    if not executable.is_file():
        print(f"[ci] installing nrfutil {tool['version']}", flush=True)
        python = shutil.which(f"python{tool['python']}")
        if not python:
            fail(f"Python {tool['python']} is required for nrfutil {tool['version']}")
        archive = download(tool)
        replace(destination)
        run([python, "-m", "venv", destination])
        environment = os.environ.copy()
        environment["PIP_CACHE_DIR"] = str(CACHE / "pip-cache")
        run(
            [
                destination / "bin" / "pip",
                "install",
                "--constraint",
                ROOT / "scripts" / "nrfutil-constraints.txt",
                archive,
            ],
            env=environment,
        )
        archive.unlink(missing_ok=True)
    else:
        print(f"[ci] cached nrfutil {tool['version']}", flush=True)
    return require(executable, "nrfutil")


def write_manifest(build, configuration, outputs):
    files = {
        output.name: {"bytes": output.stat().st_size, "sha256": digest(output)}
        for output in outputs
    }
    manifest = build / "manifest.json"
    manifest.write_text(
        json.dumps(
            {
                "configuration": configuration,
                "source_dirty": source_dirty(),
                "source_revision": source_revision(),
                "target": "nRF52832_xxAA",
                "tools": {
                    "nrf5_sdk": CONFIG["nrf5_sdk"]["version"],
                    "nrf_command_line_tools": CONFIG["nrf_command_line_tools"][
                        "version"
                    ],
                    "nrfutil": CONFIG["nrfutil"]["version"],
                    "segger_embedded_studio": CONFIG["segger_embedded_studio"][
                        "version"
                    ],
                },
                "files": files,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    checksums = build / "SHA256SUMS"
    checksum_targets = [*outputs, manifest]
    checksums.write_text(
        "".join(f"{digest(path)}  {path.name}\n" for path in checksum_targets),
        encoding="utf-8",
    )
    return manifest, checksums


def main(configuration):
    if platform.system() != "Linux" or platform.machine() not in {"x86_64", "amd64"}:
        fail("CI requires Linux amd64; use act on Apple Silicon")
    if os.environ.get("CI_ACCEPT_SEGGER_LICENSE") != "1":
        fail("set CI_ACCEPT_SEGGER_LICENSE=1 after confirming SEGGER license coverage")

    CACHE.mkdir(parents=True, exist_ok=True)
    build = BUILD_ROOT / configuration
    replace(build)
    build.mkdir(parents=True)
    workspace = None
    try:
        workspace = setup_sdk()
        embuild = setup_ses()
        cli_root, mergehex = setup_nrf_cli()
        nrfutil = setup_nrfutil()

        project_config = CONFIG["project"]
        project = require(workspace / project_config["project_file"], "SES project")
        run(
            [embuild, "-config", configuration, "-rebuild", project],
            cwd=project.parent,
        )

        application_path = project_config["application_hex"].format(
            configuration=configuration
        )
        application = require(workspace / application_path, "application HEX")
        application_out = build / "application.hex"
        shutil.copy2(application, application_out)

        package = CONFIG["packaging"]
        softdevice = require(ROOT / package["softdevice"], "SoftDevice HEX")
        bootloader = require(ROOT / package["bootloader"], "bootloader HEX")
        if digest(softdevice) != package["softdevice_sha256"]:
            fail("SoftDevice checksum mismatch")
        if digest(bootloader) != package["bootloader_sha256"]:
            fail("bootloader checksum mismatch")

        settings = build / "bootloader-settings.hex"
        merged = build / "merged-flash.hex"
        dfu = build / "firmware-dfu.zip"
        key = require(ROOT / package["signing_key"], "DFU signing key")
        run(
            [
                nrfutil,
                "settings",
                "generate",
                "--family",
                "NRF52",
                "--application",
                application_out,
                "--application-version",
                package["application_version_settings"],
                "--bootloader-version",
                package["bootloader_version_settings"],
                "--bl-settings-version",
                package["bootloader_settings_version"],
                settings,
            ]
        )
        cli_environment = os.environ.copy()
        cli_environment["LD_LIBRARY_PATH"] = str(cli_root / "lib")
        run(
            [
                mergehex,
                "-m",
                softdevice,
                application_out,
                bootloader,
                settings,
                "-o",
                merged,
            ],
            env=cli_environment,
        )
        run(
            [
                nrfutil,
                "pkg",
                "generate",
                "--hw-version",
                package["hardware_version"],
                "--sd-req",
                package["softdevice_requirement"],
                "--application-version",
                package["application_version_package"],
                "--application",
                application_out,
                "--key-file",
                key,
                dfu,
            ]
        )

        outputs = (application_out, settings, merged, dfu)
        for output in outputs:
            require(output, "CI output")
        run([nrfutil, "pkg", "display", dfu])
        write_manifest(build, configuration, outputs)
        print(f"[ci] configuration:      {configuration}")
        print(f"[ci] application sha256: {digest(application_out)}")
        print(f"[ci] merged sha256:      {digest(merged)}")
        print("[ci] passed; production-signed DFU ZIP is deployable")
    finally:
        if workspace is not None:
            replace(workspace)


if __name__ == "__main__":
    try:
        main(parse_args().configuration)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"[ci] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
