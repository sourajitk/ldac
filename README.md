# LDAC for Windows

An open-source Bluetooth audio driver and codec implementation for Windows 10 and 11 that enables Sony LDAC audio up to 96 kHz / 24-bit at 990 kbps.

Windows natively defaults to standard SBC or AAC over Bluetooth. This project provides a full LDAC encoder and decoder pipeline, a user-mode streaming utility with a control panel, and a KMDF kernel filter driver to route LDAC directly to compatible headphones like the Sony WH-1000XM4/XM5, WF-1000XM4/XM5, and external LDAC DACs.

## What is in this repository

- **libldac**: Official Sony AOSP encoder port and clean-room decoder. Supports 990 kbps (High Quality), 660 kbps (Standard), 330 kbps (Mobile), and dynamic Adaptive Bitrate (ABR).
- **Control Panel GUI**: A lightweight dark-mode interface with dual-channel VU meters, bitrate switching, and system tray minimization.
- **CLI Utility**: Command-line tool to scan paired Bluetooth devices, verify codec support, stream audio, encode WAV files, and run benchmarks.
- **Background Service**: Windows service daemon that monitors Bluetooth connections and automatically routes audio when headphones turn on.
- **KMDF Kernel Filter Driver**: An upper filter driver that intercepts Bluetooth Request Blocks (BRBs) on the Windows A2DP audio stack to bypass user-mode L2CAP socket restrictions.
- **CI/CD Pipeline**: GitHub Actions workflow that builds the driver and integrates with SignPath.io for open-source EV code signing.

## Quick Start

### 1. Check Paired Devices

Run the CLI tool to list paired Bluetooth devices and check for LDAC support:

```cmd
bin\ldac_cli.exe devices
```

### 2. Check Codec Status

Query your headphone endpoints and verify if LDAC is active:

```cmd
bin\ldac_cli.exe check
```

### 3. Stream Live Audio

To stream system audio directly to your headphones from the command line:

```cmd
bin\ldac_cli.exe stream <device-mac-address> --bitrate 990 --rate 96000
```

Add `--abr` if you want dynamic bitrate adjustment in noisy wireless environments.

### 4. Control Panel GUI

Launch the desktop interface:

```cmd
bin\ldac_control_panel.exe
```

Select your device from the dropdown, choose your bitrate mode, and click Start. Minimizing the window sends it to the Windows notification area.

### 5. File Conversion

Encode a WAV file to LDAC:

```cmd
bin\ldac_cli.exe encode input.wav output.ldac --bitrate 990
```

Decode an LDAC stream back to 96 kHz 24-bit WAV:

```cmd
bin\ldac_cli.exe decode input.ldac output.wav
```

### 6. Benchmark

Measure encoding and decoding speed on your machine:

```cmd
bin\ldac_cli.exe benchmark
```

## How to Verify LDAC on Your Device

If you are using Sony headphones like the WH-1000XM4 or WH-1000XM5:

1. Open the Sony Headphones Connect app on your phone while connected to your PC.
2. Check the top banner next to the battery percentage. It will display the white LDAC badge instead of SBC or AAC.
3. Make sure "Connect to 2 devices simultaneously" (multipoint) is turned off in the app settings, as Sony firmware disables LDAC whenever multipoint is enabled.
4. Set "Bluetooth Connection Quality" to "Priority on Sound Quality".

## Kernel Filter Driver and Code Signing

Windows restricts user-mode applications from creating raw L2CAP sockets over Bluetooth, keeping AVDTP audio locked inside the kernel driver BthA2dp.sys.

To address this without third-party paywalls, this repository includes a KMDF Upper Filter driver located in `driver/kernel/`:

- `ldac_filter.c`: Device initialization and request routing.
- `ldac_brb.c`: BRB submission interceptor for L2CAP channels.
- `ldac_avdtp.c`: Codec capability negotiation for Sony Vendor ID 0x012D and Codec ID 0x00AA.
- `ldac_filter.inf`: Driver installation file targeting BTHENUM\{0000110b-0000-1000-8000-00805f9b34fb}.

### Building the Driver

Open a Developer Command Prompt for Visual Studio 2022 with the Windows Driver Kit installed:

```cmd
msbuild driver\kernel\ldac_filter.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Signing and Installation

Because 64-bit Windows requires kernel drivers to be signed, you have two options:

- **Free Open-Source Signing**: This repository includes a GitHub Actions workflow in `.github/workflows/build_and_sign.yml` that packages the driver into a CAB file and requests an open-source EV signature through SignPath.io. See `docs/signing_guide.md` for full instructions.
- **Local Test Signing**: If you have test mode enabled (`bcdedit /set testsigning on`), you can sign the binary with a local certificate for development testing.

## Building the User-Mode Tools

To build the DLL, CLI, service, and control panel on Windows, run:

```cmd
build.bat
```

Requires Clang/LLVM or Visual Studio 2022 Community. All compiled binaries are placed in `bin/`.

## License

This project incorporates code from the Android Open Source Project (Apache 2.0) and clean-room reverse engineering implementations. See individual source headers for details.
