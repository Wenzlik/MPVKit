> **Wenzlik/MPVKit fork.** Upstream MPVKit 1.0.0 with FFmpeg rebuilt from
> n8.1.3 on upstream's configure line plus `--enable-encoder=eac3` and
> `--enable-filter=yadif_videotoolbox` and one source patch (below), for
> PrismCore. Only the seven LGPL FFmpeg xcframeworks come from this fork's
> releases; libmpv and every other binary are upstream's 1.0.0. The package
> identity stays `mpvkit` so it replaces upstream in a dependency graph.
> See [Fork notes](#fork-notes).

# MPVKit

[![mpv](https://img.shields.io/badge/mpv-v0.41.0-blue.svg)](https://github.com/mpv-player/mpv)
[![ffmpeg](https://img.shields.io/badge/ffmpeg-n8.1.3-blue.svg)](https://github.com/FFmpeg/FFmpeg)
[![license](https://img.shields.io/github/license/mpvkit/MPVKit)](https://github.com/mpvkit/MPVKit/main/LICENSE)

> MPVKit is only suitable for learning `libmpv` and will not be maintained too frequently.

`MPVKit` is a collection of tools to use `mpv` in `iOS`, `macOS`, `tvOS` applications.

It includes scripts to build `mpv` native libraries.

Forked from [kingslay/FFmpegKit](https://github.com/kingslay/FFmpegKit)

## About Metal support

Metal support only a patch version ([#7857](https://github.com/mpv-player/mpv/pull/7857)) and does not officially support it yet. Encountering any issues is not strange. 

## Fork notes

**Build identity.** `av_version_info()` = `n8.1.3-eac3-vt-sa.1`
(`eac3` encoder, `vt` = yadif_videotoolbox, `sa` = skip_attachments patch;
the trailing number counts rebuilds of the same recipe).

**Patches carried** (`Sources/BuildScripts/patch/FFmpeg/*.patch`, applied with
`git apply` onto the `n8.1.3` tag before configure):

- `0001-matroskadec-skip_attachments.patch` adds a private option
  `skip_attachments` (bool, default 0) to the `matroska,webm` demuxer. With 1
  the Attachments element (fonts, cover art) is not read: before the first
  Cluster it is stepped over by its size with one real seek (on a seekable
  input; a non-seekable one can only read through it), and when referenced
  from the SeekHead it is not visited; no attachment streams are created. Attachment streams sit
  after the track streams, so track stream indices do not change. Unknown-size
  or truncated Attachments elements are parsed as usual. With 0 or unset the
  demuxer behaves as stock. Use: `av_dict_set(&opts, "skip_attachments", "1", 0)`
  for `avformat_open_input`.

**Reproduce the build.** Clone to a path without spaces (FFmpeg's configure
breaks on them), install the Metal toolchain under the Xcode you build with
(`xcodebuild -downloadComponent MetalToolchain`, otherwise configure silently
drops yadif_videotoolbox), then:

```
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer make build version=1.0.2
```

The build area is `./dist`; xcframeworks land in `dist/release/xcframework`.

**Verify** (macOS host, after the build; needs Homebrew `ffmpeg` for fixtures,
`pkg-config`, `python3`):

```
Sources/BuildScripts/verify/verify.sh dist
```

It checks every slice's `config_components.h` for the eac3 encoder and
yadif_videotoolbox, that the xcframeworks have exactly the expected
platform/variant/arch slices, and each slice binary for both symbols; then links a small C program against the macOS
xcframework slice and checks the version stamp, the option (default 0), and,
on generated MKVs with a 20 MB or a 16 KiB attachment, before the first
Cluster or reachable only through the SeekHead: unset/0/1 give identical
tracks, packets, chapters and seeks; 1 creates no attachment streams, and
while opening no read starts inside the attachment payload.

**LGPL.** The recipe passes `--enable-version3` (and no `--enable-gpl`), so
FFmpeg's configure licenses this build as LGPL version 3 or later
(LGPL-3.0-or-later); the patch itself is LGPL-2.1-or-later like the file it
changes. Corresponding source is
upstream FFmpeg tag `n8.1.3` plus the patches above; the exact configure line
is `BuildFFMPEG` in `Sources/BuildScripts/XCFrameworkBuild/main.swift`. Ship
both (or a link to this repository at the release tag) with any binary
distribution.

## Installation

### Swift Package Manager

```
https://github.com/mpvkit/MPVKit.git
```

### Choose which version

| Version | License | Note |
|---|---|---|
| MPVKit | LGPL | [FFmpeg details](https://github.com/FFmpeg/FFmpeg/blob/master/LICENSE.md) , [mpv details](https://github.com/mpv-player/mpv/blob/master/Copyright) |
| MPVKit-GPL | GPL | Support samba protocol, same as old MPVKit version |


## How to build

```bash
make build
# specified platforms (ios,macos,tvos,tvsimulator,isimulator,maccatalyst,xros,xrsimulator)
make build platform=ios,macos
# build GPL version
make build enable-gpl
# clean all build temp files and cache
make clean
# see help
make help
```

## Make demo app using the local build version

If you want the demo app to use the local build version, you need to modify `Package.swift` to reference the local build xcframework file.

<details>
<summary>Click here for more information.</summary>
  
```
.binaryTarget(
    name: "Libmpv-GPL",
    path: "dist/release/Libmpv.xcframework.zip"
),
.binaryTarget(
    name: "Libavcodec-GPL",
    path: "dist/release/Libavcodec.xcframework.zip"
),
.binaryTarget(
    name: "Libavdevice-GPL",
    path: "dist/release/Libavdevice.xcframework.zip"
),
.binaryTarget(
    name: "Libavformat-GPL",
    path: "dist/release/Libavformat.xcframework.zip"
),
.binaryTarget(
    name: "Libavfilter-GPL",
    path: "dist/release/Libavfilter.xcframework.zip"
),
.binaryTarget(
    name: "Libavutil-GPL",
    path: "dist/release/Libavutil.xcframework.zip"
),
.binaryTarget(
    name: "Libswresample-GPL",
    path: "dist/release/Libswresample.xcframework.zip"
),
.binaryTarget(
    name: "Libswscale-GPL",
    path: "dist/release/Libswscale.xcframework.zip"
),
```

</details>

## Run default mpv player

```bash
./mpv.sh --input-commands='script-message display-stats-toggle' [url]
./mpv.sh --list-options
```

> Use <kbd>Shift</kbd>+<kbd>i</kbd> to show stats overlay

## Related Projects

* [moltenvk-build](https://github.com/mpvkit/moltenvk-build)
* [libplacebo-build](https://github.com/mpvkit/libplacebo-build)
* [libdovi-build](https://github.com/mpvkit/libdovi-build)
* [libshaderc-build](https://github.com/mpvkit/libshaderc-build)
* [libluajit-build](https://github.com/mpvkit/libluajit-build)
* [libass-build](https://github.com/mpvkit/libass-build)
* [libbluray-build](https://github.com/mpvkit/libbluray-build)
* [libsmbclient-build](https://github.com/mpvkit/libsmbclient-build)
* [gnutls-build](https://github.com/mpvkit/gnutls-build)
* [openssl-build](https://github.com/mpvkit/openssl-build)

## Donation

If you appreciate my current work, you can buy me a cup of coffee ☕️.

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/C0C410P7UN)

## License

`MPVKit` source alone is licensed under the LGPL v3.0.

`MPVKit` bundles (`frameworks`, `xcframeworks`), which include both `libmpv` and `FFmpeg` libraries, are also licensed under the LGPL v3.0. However, if the source code is built using the optional `enable-gpl` flag or prebuilt binaries with `-GPL` postfix are used, then `MPVKit` bundles become subject to the GPL v3.0.
