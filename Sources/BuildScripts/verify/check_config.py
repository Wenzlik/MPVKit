#!/usr/bin/env python3
"""Check every FFmpeg slice of a build area (default ./dist): each expected
platform/arch has a config.h with the eac3 encoder and yadif_videotoolbox,
and every slice of Libavcodec/Libavfilter.xcframework has the symbols."""
import glob, os, plistlib, subprocess, sys

DIST = sys.argv[1] if len(sys.argv) > 1 else "dist"
SLICES = {  # PlatformType.architectures in XCFrameworkBuild/base.swift
    "ios": ["arm64"], "isimulator": ["arm64", "x86_64"],
    "tvos": ["arm64", "arm64e"], "tvsimulator": ["arm64", "x86_64"],
    "xros": ["arm64"], "xrsimulator": ["arm64"],
    "macos": ["arm64", "x86_64"], "maccatalyst": ["arm64", "x86_64"],
}
DEFINES = ["#define CONFIG_EAC3_ENCODER 1", "#define CONFIG_YADIF_VIDEOTOOLBOX_FILTER 1"]
SYMBOLS = {"Libavcodec": "_ff_eac3_encoder", "Libavfilter": "_ff_vf_yadif_videotoolbox"}

bad = 0
for platform, archs in SLICES.items():
    for arch in archs:
        path = os.path.join(DIST, "FFmpeg", platform, "scratch", arch, "config.h")
        try:
            text = open(path).read()
        except OSError:
            print(f"FAIL {platform}/{arch}: no {path}"); bad += 1; continue
        missing = [d for d in DEFINES if d not in text]
        print(f"{'FAIL' if missing else 'ok  '} {platform}/{arch} config.h {missing or ''}")
        bad += bool(missing)

for fw, sym in SYMBOLS.items():
    xcf = os.path.join(DIST, "release", "xcframework", fw + ".xcframework")
    try:
        libs = plistlib.load(open(os.path.join(xcf, "Info.plist"), "rb"))["AvailableLibraries"]
    except OSError:
        print(f"FAIL no {xcf}"); bad += 1; continue
    if len(libs) != len(SLICES):
        print(f"FAIL {fw}: {len(libs)} xcframework slices, expected {len(SLICES)}"); bad += 1
    for lib in libs:
        binary = os.path.join(xcf, lib["LibraryIdentifier"], lib["LibraryPath"], fw)
        for arch in lib["SupportedArchitectures"]:
            out = subprocess.run(["nm", "-arch", arch, "-U", binary],
                                 capture_output=True, text=True).stdout
            ok = any(line.endswith(" " + sym) for line in out.splitlines())
            print(f"{'ok  ' if ok else 'FAIL'} {lib['LibraryIdentifier']}/{arch} {fw} {sym}")
            bad += not ok

print("check_config: OK" if not bad else f"check_config: {bad} FAILURE(S)")
sys.exit(1 if bad else 0)
