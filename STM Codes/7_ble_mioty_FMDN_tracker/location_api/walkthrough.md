# Walkthrough: Google Find My Device Location API & Leaflet Dashboard

This directory implements a Python location service, a Flask REST API, and an interactive **Leaflet.js visual map dashboard** for querying tracker positions reported through the Google Find My Device Network (FMDN).

It reuses `GoogleFindMyTools` dependencies, configuration, and end-to-end encrypted Google auth keys, vendored locally at the project root (`Sx-1280-bring up local\GoogleFindMyTools`) so this project is self-contained and does not depend on any path outside the repo.

---

## 1. Directory Structure

```
location_api/
├── requirements.txt            ← Specifies dependency flask & flask-cors
├── fmdn_client.py              ← Coordinates Google Find My query & decrypts reports
├── ble_location_service.py     ← Manages device caches & retrieves most recent location
├── app.py                      ← Flask API server & static file host (port 5000)
├── static/
│   └── index.html              ← Dashboard UI (Leaflet.js map + sidebar + glassmorphism CSS)
└── walkthrough.md              ← This document
```

---

## 2. Components

### Flask Server (`app.py` & static hosting)
- Mounts static files from `location_api/static/`, served at path `/static`.
- `GET /` maps to the dashboard's HTML entry point.
- `debug=False` prevents double-execution of background FCM messaging threads (avoids socket/listener conflicts).

### Interactive Frontend Dashboard (`static/index.html`)
- **Map interaction, colors & selective rendering**:
  - Loads the registered device list on start; force-refreshes from Google with `?refresh=true` via the sidebar "Refresh" button.
  - **Dynamic color coding**: assigns a unique neon color theme per device (Cyan, Green, Gold, Coral Red, Purple), applied to its card indicators, badges, active border, map path, and marker rings.
  - **Selective rendering**: markers, accuracy circles, and history trails are shown only for the active selected device. Selecting another device clears the map and flies the view to its position.
  - **Dashed history trails**: a colored dashed line connects historical location reports chronologically.
  - **Interactive coordinate nodes**: small markers along the trail show time and accuracy in a popup on click.
  - **Pulsing latest node**: a large pulsing neon marker on the most recent position, linking to Google Maps.
  - **In-card telemetry log**: expandable, scrollable table of historical coordinates and timestamps per device.
  - Auto-refreshes in the background every 2 minutes.

---

## 3. API Endpoints

Served on port `5000`:

* **`GET /`**
  - Serves the visual tracking map dashboard.
* **`GET /api/ble/devices`**
  - Returns all registered tracker names in the active Google account.
* **`GET /api/ble/location/<device_name>`**
  - Retrieves and decrypts location telemetry for the device.
  - **Query parameters**:
    - `history=true` (optional): returns the full array of decrypted locations for the tag, sorted by time descending, in a `history` list (instead of just the single latest point).
    - `refresh=true` (optional): force-updates the local cached device list from Google before executing the query.

---

## 4. Example Requests & Responses

### `GET /`
```bash
curl.exe -s -I http://127.0.0.1:5000/
```
```http
HTTP/1.1 200 OK
Server: Werkzeug/3.1.8 Python/3.11.9
Content-Disposition: inline; filename=index.html
Content-Type: text/html; charset=utf-8
Content-Length: 25627
```

### `GET /api/ble/devices`
```bash
curl.exe -s http://127.0.0.1:5000/api/ble/devices
```
```json
{"devices":["Tracker A","Tracker B","Tracker C"],"status":"success"}
```

### `GET /api/ble/location/<device_name>`
```bash
curl.exe -s "http://127.0.0.1:5000/api/ble/location/Tracker%20A"
```
```json
{
  "accuracy_meters": 100.0,
  "altitude": 335,
  "canonic_id": "00000000-0000-0000-0000-000000000000",
  "device_name": "Tracker A",
  "google_maps_link": "https://www.google.com/maps/search/?api=1&query=<lat>,<lon>",
  "is_own_report": true,
  "latitude": 0.0,
  "longitude": 0.0,
  "source": "ble",
  "status": "LAST_KNOWN",
  "timestamp": 0,
  "timestamp_readable": "YYYY-MM-DD HH:MM:SS",
  "type": "geo"
}
```

---

## 5. First-Time Setup

Neither the virtual environment nor your Google auth token are committed to git (both are gitignored — see `GoogleFindMyTools/.gitignore`), so **every person who clones this repo needs to do this setup once**, using their own Google account. Full walkthrough (one setup script + the one-time Google login) lives in `../FMDN_tracker.md`, "Step 0: One-Time Environment Setup" — follow that first.

Short version, from this folder:
```bash
.\setup.ps1                                       # creates ./venv, installs everything
.\venv\Scripts\python.exe ..\..\GoogleFindMyTools\main.py   # one-time interactive Google login
```

You'll only see devices registered to *your own* Google account. To see a specific tracker (e.g. someone else's hardware), it needs to be registered/shared to your account first — auth tokens are inherently per-account and aren't something that should be shared between people.

## 6. How to Launch

1. From `location_api/`:
   ```bash
   .\run_dashboard.ps1
   ```
2. Flask prints its own startup output, including a `WARNING: This is a development server...` line — that's standard boilerplate, safe to ignore for local/personal use, not a sign anything is wrong. It then lists **two addresses** (`app.py` binds to `0.0.0.0`, all network interfaces, not just this machine):

   | Address | Reachable from | Use it for |
   | :--- | :--- | :--- |
   | `http://127.0.0.1:5000/` | Only this computer | Normal local use — default choice |
   | `http://<other IP>:5000/` (e.g. `192.168.x.x`) | Any device on the same WiFi/network | Checking the dashboard from another device (e.g. your phone) |

   The dashboard shows real device location data. Only use the second (LAN) address on a network you actually trust — never on shared/public WiFi (university, cafe, etc.), since anyone else on that network could browse straight to it with no login of their own required.
3. Open the address you chose in a browser. The dashboard fetches your active Google account's tracker devices and draws their positions live on the map.
4. Press `Ctrl+C` in the terminal to stop the server.
