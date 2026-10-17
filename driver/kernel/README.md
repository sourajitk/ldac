# Sony LDAC Bluetooth Audio KMDF Upper Filter Driver

This directory contains the production-grade **Kernel-Mode Driver Framework (KMDF)** Upper Filter Driver for Windows 10 and 11.

It binds to the Windows Bluetooth A2DP audio endpoint:
```text
BTHENUM\{0000110b-0000-1000-8000-00805f9b34fb} (Audio Sink)
```

---

## 🏗️ Architecture

```
+--------------------------------------------------------------+
|            Windows Audio Engine (audiodg.exe / WASAPI)       |
|                       96 kHz / 24-bit                        |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|      Microsoft Audio Class Driver (PortCls / BthA2dp.sys)     |
+--------------------------------------------------------------+
                               |
                               v  (Internal BRB IOCTLs)
+==============================================================+
|        ★ SONY LDAC KMDF UPPER FILTER (ldac_filter.sys)       |
|  - Intercepts IOCTL_INTERNAL_BTH_SUBMIT_BRB                  |
|  - Detects AVDTP DISCOVER / GET_CAPABILITIES transactions    |
|  - Injects Sony LDAC SEP (Vendor: 0x012D, Codec: 0x00AA)     |
|  - Forces 96 kHz / 24-bit / 990 kbps negotiation             |
|  - Dispatches BRB_L2CA_ACL_TRANSFER to Bluetooth stack       |
+==============================================================+
                               |
                               v
+--------------------------------------------------------------+
|            Windows Bluetooth Protocol Stack (BthPort)        |
+--------------------------------------------------------------+
```

---

## 📁 Source Files

| File | Description |
| :--- | :--- |
| `ldac_filter.h` | Core definitions, device context, and BRB function declarations. |
| `ldac_filter.c` | KMDF `DriverEntry`, `EvtDeviceAdd`, and default queue handlers. |
| `ldac_brb.c` | Bluetooth Request Block (BRB) interceptor and L2CAP dispatch engine. |
| `ldac_avdtp.c` | AVDTP signaling packet parser and Sony LDAC capability injector. |
| `ldac_filter.inf` | PnP UpperFilter driver installation INF. |
| `ldac_filter.vcxproj` | MSBuild / Visual Studio project file configured for Windows 10/11 WDK. |

---

## 🔨 Compiling Locally with the Windows Driver Kit (WDK)

1. Ensure Visual Studio 2022 Community and the **Windows 11 WDK (10.0.22621.0+)** are installed.
2. Open **Developer Command Prompt for VS 2022**:
   ```cmd
   msbuild driver\kernel\ldac_filter.vcxproj /p:Configuration=Release /p:Platform=x64
   ```
3. Output files will be generated in `bin\Release\`:
   * `ldac_filter.sys` (Kernel Driver binary)
   * `ldac_filter.inf` (PnP INF)
   * `ldac_filter.cat` (Package Catalog file)

---

## 🚀 Automated CI/CD & Microsoft Attestation Signing

All builds, packaging, and signing requests are fully automated in `.github/workflows/build_and_sign.yml`.
When pushed to GitHub, the workflow builds the driver on Microsoft's cloud runners and submits signing requests via **SignPath.io** (or an EV certificate secret).
