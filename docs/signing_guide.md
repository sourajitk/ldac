# Open-Source Windows Driver Signing Guide

This guide explains how to get the **LDAC-Windows Kernel Filter Driver (`ldac_filter.sys`)** officially signed by Microsoft for **100% free** using the **SignPath.io Open Source Foundation**, allowing it to install on any standard Windows 10/11 machine without warnings, without test mode, and with Secure Boot enabled.

---

## 🏛️ The Signing Pipeline Overview

```
GitHub Repository (Push / Tag)
       │
       ▼
GitHub Actions CI/CD (.github/workflows/build_and_sign.yml)
       │ (Compiles user-mode suite + KMDF kernel filter driver)
       │ (Builds package/ldac_filter_attestation.cab)
       ▼
SignPath.io Open Source Foundation
       │ (Signs .cab with an authorized EV Code Signing Certificate)
       ▼
Microsoft Hardware Developer Center (WHQL Attestation)
       │ (Automated security scan & Microsoft Production PCA countersigning)
       ▼
Final Certified Driver Package
       (Installs on any Windows 10 & 11 machine worldwide)
```

---

## 🆓 Step 1: Apply for Free Open-Source Signing at SignPath.io

[SignPath.io](https://about.signpath.io/programs/open-source) provides free EV code signing certificates and Microsoft WHQL submission for vetted open-source projects.

1. **Host on GitHub**: Ensure this repository is published publicly on GitHub (e.g. `https://github.com/your-username/LDAC-Windows`).
2. **Apply**: Visit [https://about.signpath.io/programs/open-source](https://about.signpath.io/programs/open-source) and click **Apply for Free Open Source Signing**.
3. **Fill out the form**:
   * Project Name: `LDAC-Windows`
   * Project Description: `Free and open-source high-resolution Sony LDAC Bluetooth A2DP audio driver for Windows 10 and 11.`
   * License: `MIT` / `Apache-2.0` / `GPL-3.0`
4. Once approved, SignPath will create your project in their portal and provide:
   * **Organization ID**
   * **Project Slug** (e.g. `ldac-windows`)
   * **Signing Policy Slug** (e.g. `release-signing`)
   * **API Token**

---

## 🔐 Step 2: Add GitHub Repository Secrets

In your GitHub repository:
1. Go to **Settings** &rarr; **Secrets and variables** &rarr; **Actions**.
2. Click **New repository secret** and add the following 4 secrets:

| Secret Name | Value | Description |
| :--- | :--- | :--- |
| `SIGNPATH_API_TOKEN` | `<your-api-token>` | API token generated in SignPath.io settings. |
| `SIGNPATH_ORGANIZATION_ID`| `<your-org-uuid>` | Your SignPath Organization ID. |
| `SIGNPATH_PROJECT_SLUG` | `ldac-windows` | Project slug in SignPath. |
| `SIGNPATH_SIGNING_POLICY_SLUG` | `release-signing` | Policy slug for release builds. |

---

## 🚀 Step 3: Trigger the Build and Sign Workflow

Whenever you push code or create a release tag:
```bash
git tag v1.0.0
git push origin v1.0.0
```

1. GitHub Actions automatically executes `.github/workflows/build_and_sign.yml`.
2. The workflow compiles the user-mode libraries, tests, GUI, and the kernel filter driver `ldac_filter.sys`.
3. It packages the driver into `package/ldac_filter_attestation.cab`.
4. It calls the SignPath action to digitally sign the CAB file using the EV certificate.
5. The signed package is made available for download in the **Artifacts** tab and published to your GitHub Release!

---

## 🏢 Alternative: If You Own a Commercial EV Certificate

If you or your company already possesses an EV Code Signing Certificate (PFX file or cloud HSM token):

1. Base64-encode your PFX certificate file:
   ```powershell
   [Convert]::ToBase64String([IO.File]::ReadAllBytes("MyEvCert.pfx")) | Set-Clipboard
   ```
2. Add the following secrets to GitHub:
   * `EV_CERT_BASE64`: The Base64 string of the PFX file.
   * `EV_CERT_PASSWORD`: The password for the certificate.
3. The workflow will automatically detect these secrets and sign both `ldac_filter.sys` and the `.cab` submission archive using `signtool.exe`.

---

## 📥 Submitting to Microsoft Hardware Dev Center

Once your `.cab` file is signed with an EV certificate (via SignPath or your own cert):
1. Sign in to the [Microsoft Partner Center Hardware Dashboard](https://partner.microsoft.com/dashboard/hardware/).
2. Click **Drivers** &rarr; **Submit new driver**.
3. Upload `package/ldac_filter_attestation.cab`.
4. Check **Attestation Signing** and select Windows 10 and 11 compatibility.
5. In ~15 to 45 minutes, Microsoft's automated system verifies the driver and issues the signed zip containing the Microsoft-certified catalog (`.cat`) and driver package.
6. Users can now install `ldac_filter.sys` with **one click** on any Windows machine!
