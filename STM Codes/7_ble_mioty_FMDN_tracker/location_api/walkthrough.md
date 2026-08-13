# Walkthrough: Google Find My Device Location API & Leaflet Dashboard

This directory implements a Python location service, a Flask REST API, and an interactive **Leaflet.js visual map dashboard** for querying tracker positions reported through the Google Find My Device Network (FMDN).

It reuses the sibling `GoogleFindMyTools` dependencies, configuration, and end-to-end encrypted Google auth keys, without modifying any third-party code.

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

## 5. How to Launch

1. In your shell, navigate to the `location_api` directory.
2. Run the server using the Python interpreter from `GoogleFindMyTools`'s virtual environment (adjust the path to match your own `GoogleFindMyTools` checkout location):
   ```bash
   & "<path-to-GoogleFindMyTools>\venv\Scripts\python.exe" app.py
   ```
3. Open a browser to:
   ```url
   http://127.0.0.1:5000/
   ```
   The dashboard fetches your active Google account's tracker devices and draws their positions live on the map.
