"""Compile the exact custom-integration identifiers emitted by the pinned NRD SDK."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    for name in ("manifest-tool", "dxc", "sdk", "mathlib", "config", "output"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    pending = output / "nrd-relax.pending.txt"
    subprocess.run([args.manifest_tool, str(pending)], check=True)
    identifiers = pending.read_text(encoding="utf-8").splitlines()
    if len(identifiers) != 27:
        raise ValueError("Pinned RELAX pipeline count changed; update the build output contract")
    for index, identifier in enumerate(identifiers):
        source, *defines = identifier.split("|")
        if not re.fullmatch(r"(?:RELAX_\w+|Clear)\.cs\.hlsl", source):
            raise ValueError("Unexpected shader identifier: " + identifier)
        if any(not re.fullmatch(r"\w+=\w+", define) for define in defines):
            raise ValueError("Malformed shader definition: " + identifier)
        command = [args.dxc, "-nologo", "-T", "cs_6_0", "-E", "main", "-O3",
                   "-all_resources_bound", "-DNRD_INTERNAL", "-I" + args.config,
                   "-I" + args.mathlib, "-Fo", str(output / f"nrd-relax-{index}.cso"),
                   str(Path(args.sdk) / "Shaders" / source)]
        command += ["-D" + define for define in defines]
        subprocess.run(command, check=True)
    pending.replace(output / "nrd-relax-manifest.txt")
    print(f"Compiled {len(identifiers)} pinned RELAX pipelines")


if __name__ == "__main__":
    main()
