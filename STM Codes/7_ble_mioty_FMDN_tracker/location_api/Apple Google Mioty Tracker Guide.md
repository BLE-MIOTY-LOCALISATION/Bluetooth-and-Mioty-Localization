# Dual-Network Tracker Dashboard: Google Find My + Apple Find My + Mioty

This directory implements a Python location service, a Flask REST API, and an interactive **Leaflet.js visual map dashboard** for querying tracker positions crowdsourced across both the **Google Find My Device Network (FMDN)** and the **Apple Find My Network (Offline Finding)**, alongside Mioty long-range RF capabilities.

---

## 1. Directory Structure

```
location_api/
├── requirements.txt            ← Specifies Flask dependencies
├── fmdn_client.py              ← Coordinates Google Find My query & decrypts reports
├── ble_location_service.py     ← Aggregates & decrypts Google + Apple telemetry
├── device_hardware_config.json ← Flashed hardware specs, EIDs, MACs, & keys
├── app.py                      ← Flask API server & static file host (port 5000)
├── setup.ps1                   ← One-time virtualenv setup script
├── run_dashboard.ps1           ← Quick-launcher script
├── static/
│   ├── index.html              ← Landing page / Key generation wizard
│   └── dashboard.html          ← Dual-network map dashboard + Tag Keys modal + Telemetry CSV
└── walkthrough.md              ← This document
```

---

## 2. First-Time Setup for Cloners / Colleagues

> [!NOTE]
> All live session tokens, keys, and credentials (`secrets.json`, `auth.json`, `keys.json`, `.pem`) are intentionally **git-ignored** for privacy and security. Follow these steps to initialize your local instance.

### Prerequisites
1. **Windows 10/11** with PowerShell
2. **Python 3.10+** (added to PATH)
3. **Google Chrome** (for Google Find My interactive OAuth login)
4. **Docker Desktop** (for Apple Anisette v3 server)

---

### Step 1: Start Apple Anisette v3 Server (Docker)
Apple Find My queries require authentic Anisette headers (ADI). Run this once:
```powershell
docker run -d --restart always --name anisette -p 6969:6969 dadoum/anisette-v3-server:latest
```
*Verify:* Open `http://127.0.0.1:6969/v3/reprovision` in a browser — it should return valid JSON.

---

### Step 2: Initialize Python Virtual Environment
From `STM Codes\7_ble_mioty_FMDN_tracker\location_api`:
```powershell
.\setup.ps1
```
Install the Apple endpoint dependencies:
```powershell
.\venv\Scripts\pip.exe install -r ..\..\..\macless-haystack\endpoint\requirements.txt
```

---

### Step 3: Google Find My Device Authentication
Log into the shared project Google account:
```powershell
.\venv\Scripts\python.exe ..\..\GoogleFindMyTools\main.py
```
- A Chrome window opens automatically.
- Sign in with the project Google account.
- This creates your local `GoogleFindMyTools\Auth\secrets.json`.

---

### Step 4: Apple Find My Authentication & Keys
1. In `macless-haystack\endpoint\data\`:
   - Copy `config.ini.example` to `config.ini`.
   - Set `appleid = YOUR_PROJECT_APPLE_ID@example.com`.
2. In `macless-haystack\endpoint\data\keys.json`:
   - Create `keys.json` with the shared base64 private key:
     ```json
     [
       {
         "privateKey": "yBPO7ho0j+BXdGmiRge+NtcY0Vw3hlXsU67SIw=="
       }
     ]
     ```
3. Authenticate with Apple 2FA:
   ```powershell
   cd ..\..\..\macless-haystack\endpoint
   ..\..\STM Codes\7_ble_mioty_FMDN_tracker\location_api\venv\Scripts\python.exe mh_endpoint.py
   ```
   - Enter password and the 2FA SMS/device verification code when prompted in the terminal.
   - Once authenticated, it saves `data/auth.json` and listens on port `6176`.
   - **Keep this terminal window running!**

---

### Step 5: Launch the Dashboard
In a separate terminal:
```powershell
cd "STM Codes\7_ble_mioty_FMDN_tracker\location_api"
.\run_dashboard.ps1
```
Open **[http://127.0.0.1:5000/dashboard](http://127.0.0.1:5000/dashboard)** in your browser!

---

## 3. Flashing Hardware (STM32 + SX1280)

- **Firmware Project:** [`STM Codes/10_apple_google_mioty_tracker/`](file:///c:/Users/rohit/OneDrive/Documents/FAU/BLE%20MIOTY/Bluetooth-and-Mioty-Localization/STM%20Codes/10_apple_google_mioty_tracker)
- **Parameters:** Click the **Tag Keys** button on the web dashboard to inspect the exact C arrays for:
  - Google Ephemeral ID (`fmdn_eid[20]`)
  - Apple Find My public key & bound MAC address (`mac[5] = pub_key[0] | 0xC0`)
  - Mioty EUI64 and 128-bit network encryption key
