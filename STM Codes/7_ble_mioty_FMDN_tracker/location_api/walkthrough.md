# Walkthrough: Google Find My Device Location API & Leaflet Dashboard

We have successfully implemented the Python Location Service, the Flask REST API, and a fully interactive **Leaflet.js visual map dashboard** in the project folder: [location_api](file:///c:/Users_windows/Chandu%20B%20Reddy/Projects/LOcalee/Board-standalone-bring-up/MIOTY_BLE_Tests/FMDN_tracker/location_api).

This setup reuses the sibling `GoogleFindMyTools` dependencies, configuration, and end-to-end encrypted Google auth keys, but operates without modifying any third-party code.

---

## 1. Directory Structure

The location service files are organized as follows:

```
location_api/
├── requirements.txt            ← Specifies dependency flask & flask-cors
├── fmdn_client.py              ← Coordinates Google Find My query & decrypts reports
├── ble_location_service.py     ← Manages device caches & retrieves most recent location
├── app.py                      ← Flask API server & static file host (port 5000)
├── static/
│   └── index.html              ← Dashboard UI (Leaflet.js map + sidebar + glassmorphism CSS)
└── walkthrough.md              ← This walkthrough document
```

---

## 2. Components Added & Modified

### Flask Server (`app.py` & static hosting)
- Configured Flask to mount static files located in `location_api/static/` and serve them from path `/static`.
- Added index route `GET /` which maps directly to the dashboard's HTML entry point.
- Set `debug=False` to prevent double-execution of background FCM messaging threads (which was causing socket/listener conflicts).

### Interactive Frontend Dashboard (`static/index.html`)
- **Map Interaction, Colors & Selective Rendering**:
  - Automatically loads the registered device list (and force-refreshes it from Google with `?refresh=true` when clicking the sidebar "Refresh" button).
  - **Dynamic Color Coding**: Assigns a unique neon color theme to each device (Cyan, Green, Gold, Coral Red, Purple) to brand its card indicators, badges, active border, map path, and marker rings, instantly resolving which markers belong to which device.
  - **Selective Rendering (De-cluttering)**: The map displays markers, accuracy circles, and history trails *only* for the active selected device, keeping the interface completely clean. Clicking another device clears the map and flies the view to the new device's position.
  - **Dashed History Trails**: Renders a custom colored dashed line connecting all historical location reports chronologically.
  - **Interactive Coordinate Nodes**: Places small solid markers on older positions along the path trail, showing precise time and accuracy parameters in popups when clicked.
  - **Pulsing Latest Node**: Renders a large pulsing neon locator marker matching the device color on the latest coordinates, linking directly to Google Maps.
  - **In-card Telemetry Log**: Expands the card details to show a scrollable, clean table of all historical coordinates and timestamps with scrollbar support.
  - **Layout & Scroll Fix**: Set `flex-shrink: 0` on cards to prevent flexbox shrinking and clipping of card fields (like Accuracy, Altitude, or Status) and added bottom padding to the list container so cards can be scrolled completely above the footers.
  - Auto-refreshes in the background every 2 minutes.

---

## 3. API Endpoints Served

The Flask app serves the following endpoints on port `5000`:

* **`GET /`**
  - **Description**: Serves the visual tracking map dashboard.
* **`GET /api/ble/devices`**
  - **Description**: Returns all registered tracker names in the active Google account.
* **`GET /api/ble/location/<device_name>`**
  - **Description**: Retrieves and decrypts location telemetry for the device.
  - **Query Parameters**:
    - `history=true` (optional): Returns the full array of decrypted locations recorded for the tag (instead of just the single latest point) in a `history` list sorted by time descending.
    - `refresh=true` (optional): Force-updates the local cached device list from Google before executing the query.

---

## 4. Verification & Verification Results

### Serving the Dashboard Page (`GET /`)
We tested serving the dashboard landing page at the root URL:
```bash
curl.exe -s -I http://127.0.0.1:5000/
```
**Response**:
```http
HTTP/1.1 200 OK
Server: Werkzeug/3.1.8 Python/3.11.9
Content-Disposition: inline; filename=index.html
Content-Type: text/html; charset=utf-8
Content-Length: 25627
```

### Devices List Query (`GET /api/ble/devices`)
```bash
curl.exe -s http://127.0.0.1:5000/api/ble/devices
```
**Response**:
```json
{"devices":["Galaxy Tab S9 FE","Oppo Reno 11","GoogleFindMyTools \u00b5C"],"status":"success"}
```

### Coordinates Query (`GET /api/ble/location/Galaxy%20Tab%20S9%20FE`)
```bash
curl.exe -s "http://127.0.0.1:5000/api/ble/location/Galaxy%20Tab%20S9%20FE"
```
**Response**:
```json
{
  "accuracy_meters": 100.0,
  "altitude": 335,
  "canonic_id": "67b576c0-0000-264b-93b7-582429aecb68",
  "device_name": "Galaxy Tab S9 FE",
  "google_maps_link": "https://www.google.com/maps/search/?api=1&query=49.5980236,11.0018803",
  "is_own_report": true,
  "latitude": 49.5980236,
  "longitude": 11.0018803,
  "source": "ble",
  "status": "LAST_KNOWN",
  "timestamp": 1782962281,
  "timestamp_readable": "2026-07-02 05:18:01",
  "type": "geo"
}

```

---

## 5. How to Launch

1. In your command shell (PowerShell/CMD), navigate to the `location_api` directory.
2. Run:
   ```bash
   & "C:\Users_windows\Chandu B Reddy\Projects\GoogleFindMyTools\venv\Scripts\python.exe" app.py
   ```
3. Open your web browser and navigate to:
   ```url
   http://127.0.0.1:5000/
   ```
   *(The dashboard will automatically fetch your active Google account's tracker devices list, retrieve their positions, and draw them live on the map)*.
