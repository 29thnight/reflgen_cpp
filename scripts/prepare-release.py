#!/usr/bin/env python3
"""Validate already-tested CI packages and prepare a reviewable release bundle.

This does not publish a release, create a tag, or rebuild the packages.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile


def version_of_header(text: str) -> str:
    match = re.search(r'version_string\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"', text)
    if not match:
        raise ValueError("version_string is missing")
    return match.group(1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--run-url", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        raise ValueError("an exact commit SHA is required")
    if not re.fullmatch(r"https://github\.com/29thnight/reflgen_cpp/actions/runs/[0-9]+", args.run_url):
        raise ValueError("run URL must identify the reflgen_cpp verification run")
    version = version_of_header((args.source / "include/reflgen/core/version.h").read_text(encoding="utf-8-sig"))
    names = [f"reflgen-{version}-windows-x64.zip", f"reflgen.{version}.nupkg", "Reflgen.VisualStudio.vsix"]
    for name in names:
        if not (args.artifacts / name).is_file():
            raise ValueError(f"missing release artifact: {name}")
    with zipfile.ZipFile(args.artifacts / names[0]) as package:
        packaged_version = version_of_header(package.read("include/reflgen/core/version.h").decode("utf-8-sig"))
        if packaged_version != version:
            raise ValueError("ZIP version differs from source")
        targets = package.read("share/reflgen/msbuild/reflgen.targets").decode("utf-8-sig")
        if "ReflgenFSharpTable" not in targets:
            raise ValueError("ZIP does not contain the F# table generation integration")
    with zipfile.ZipFile(args.artifacts / names[1]) as package:
        spec = next(name for name in package.namelist() if name.endswith(".nuspec"))
        metadata = ET.fromstring(package.read(spec))
        packaged_version = next(element.text for element in metadata.iter() if element.tag.split("}")[-1] == "version")
        if packaged_version != version:
            raise ValueError("NuGet version differs from source")
        targets = package.read("share/reflgen/msbuild/reflgen.targets").decode("utf-8-sig")
        if "ReflgenFSharpTable" not in targets:
            raise ValueError("NuGet package lacks the F# table generation integration")
    with zipfile.ZipFile(args.artifacts / names[2]) as package:
        manifest = ET.fromstring(package.read("extension.vsixmanifest"))
        identity = next(element for element in manifest.iter() if element.tag.split("}")[-1] == "Identity")
        if identity.attrib.get("Version") != version or identity.attrib.get("Publisher") != "29thnight":
            raise ValueError("VSIX identity does not match this release")
    hashes = {name: hashlib.sha256((args.artifacts / name).read_bytes()).hexdigest() for name in names}
    sums = "".join(f"{digest}  {name}\n" for name, digest in hashes.items())
    (args.artifacts / "SHA256SUMS.txt").write_text(sums, encoding="utf-8")
    notes = (args.source / f"docs/releases/{version}.md").read_text(encoding="utf-8")
    notes += (f"\n## Build provenance\n\nSource commit: `{args.commit}`. "
              f"These unchanged packages are the artifacts from the [successful verification run]({args.run_url}).\n"
              f"\n## SHA-256\n\n```text\n{sums}```\n")
    (args.artifacts / "RELEASE_NOTES.md").write_text(notes, encoding="utf-8")
    provenance = {"version": version, "tag": f"v{version}", "commit": args.commit, "run_url": args.run_url, "sha256": hashes}
    (args.artifacts / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(f"Validated reflgen {version}: {len(hashes)} unchanged packages; release notes and checksums prepared.")


if __name__ == "__main__":
    main()
