import os
import sys
from flask import Flask, jsonify, request
from flask_cors import CORS

# Add current dir to python path
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from ble_location_service import BLELocationService

# Configure Flask to locate static files in the 'static' directory
app = Flask(__name__, static_folder='static', static_url_path='/static')
# Enable CORS so any frontend dashboard can query it
CORS(app)

# Instantiate the service
ble_service = BLELocationService()

@app.route('/')
def index():
    """Serves the Landing Page index.html."""
    return app.send_static_file('index.html')

@app.route('/dashboard')
def dashboard():
    """Serves the Leaflet map dashboard.html."""
    return app.send_static_file('dashboard.html')

@app.route('/api/ble/register', methods=['POST'])
def register_device():
    """Generates a new FMDN authentication key by registering an ESP32 device."""
    try:
        from SpotApi.CreateBleDevice.create_ble_device import register_esp32
        
        print("[App] Calling register_esp32() to trigger Chrome Auth flow...")
        # This will block and launch Selenium Chrome locally
        eid_hex = register_esp32()
        
        # After successfully registering a new tracker, force a cache refresh
        ble_service.refresh_devices()
        
        return jsonify({
            "status": "success",
            "eid": eid_hex
        })
    except Exception as e:
        print(f"[App] Error in register_device: {e}")
        return jsonify({
            "status": "error",
            "message": str(e)
        }), 500

@app.route('/api/ble/devices', methods=['GET'])
def get_devices():
    """Returns a list of all registered device names."""
    try:
        # Optionally force refresh
        refresh = request.args.get('refresh', 'false').lower() == 'true'
        if refresh:
            ble_service.refresh_devices()
        return jsonify({
            "status": "success",
            "devices": list(ble_service.devices.keys())
        })
    except Exception as e:
        return jsonify({
            "status": "error",
            "message": str(e)
        }), 500

@app.route('/api/ble/location/<device_name>', methods=['GET'])
def get_ble_location(device_name):
    """Returns decrypted location telemetry (latest or history) for the device."""
    try:
        history = request.args.get('history', 'false').lower() == 'true'
        refresh = request.args.get('refresh', 'false').lower() == 'true'
        location = ble_service.get_location(device_name, history=history, force_refresh=refresh)
        return jsonify(location)
    except KeyError as e:
        return jsonify({
            "status": "error",
            "message": str(e)
        }), 404
    except TimeoutError as e:
        import traceback
        tb = traceback.format_exc()
        return jsonify({
            "status": "error",
            "message": str(e),
            "traceback": tb
        }), 504
    except Exception as e:
        import traceback
        traceback.print_exc()
        return jsonify({
            "status": "error",
            "message": str(e)
        }), 500

@app.route('/api/ble/hardware-info', methods=['GET', 'POST'])
def device_hardware_info():
    """Returns or updates hardware payload info (fmdn_eid, MAC addresses, Apple keys)."""
    import json
    cfg_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "device_hardware_config.json")
    if request.method == 'POST':
        try:
            new_data = request.get_json()
            if not new_data or not isinstance(new_data, dict):
                return jsonify({"status": "error", "message": "Invalid JSON body"}), 400
            
            # Merge or save
            existing = {}
            if os.path.exists(cfg_path):
                with open(cfg_path, 'r') as f:
                    existing = json.load(f)
            existing.update(new_data)
            with open(cfg_path, 'w') as f:
                json.dump(existing, f, indent=2)
            return jsonify({"status": "success", "message": "Hardware configuration updated successfully", "devices": existing})
        except Exception as e:
            return jsonify({"status": "error", "message": str(e)}), 500
            
    # GET
    try:
        if os.path.exists(cfg_path):
            with open(cfg_path, 'r') as f:
                data = json.load(f)
        else:
            data = {}
        return jsonify({"status": "success", "devices": data})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/ble/export/csv', methods=['GET'])
def export_csv():
    """Exports all available location telemetry across devices or for a single device as a CSV download."""
    import csv
    import io
    from flask import Response

    device_filter = request.args.get('device')
    target_devices = [device_filter] if device_filter else list(ble_service.devices.keys())

    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow([
        "device_name",
        "canonic_id",
        "source",
        "network",
        "timestamp_utc",
        "timestamp_readable",
        "latitude",
        "longitude",
        "altitude",
        "accuracy_meters",
        "status",
        "google_maps_link"
    ])

    for dev in target_devices:
        try:
            data = ble_service.get_location(dev, history=True)
            canonic_id = data.get("canonic_id", "N/A")
            for pt in data.get("history", []):
                if pt.get("type") == "geo":
                    src = pt.get("source", "unknown")
                    network_label = "Apple Find My" if src == "apple" else ("Google Find My" if src == "google" else src)
                    writer.writerow([
                        dev,
                        canonic_id,
                        src,
                        network_label,
                        pt.get("timestamp"),
                        pt.get("timestamp_readable"),
                        pt.get("latitude"),
                        pt.get("longitude"),
                        pt.get("altitude", ""),
                        pt.get("accuracy_meters"),
                        pt.get("status"),
                        pt.get("google_maps_link", "")
                    ])
        except Exception as e:
            print(f"[Export CSV] Error fetching for {dev}: {e}")

    filename = f"fmdn_locations_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv" if 'datetime' in globals() else "fmdn_locations.csv"
    from datetime import datetime as dt_module
    filename = f"fmdn_locations_{dt_module.now().strftime('%Y%m%d_%H%M%S')}.csv"

    return Response(
        output.getvalue(),
        mimetype="text/csv",
        headers={"Content-Disposition": f"attachment; filename={filename}"}
    )

if __name__ == '__main__':
    port = int(os.environ.get("PORT", 5000))
    app.run(host='0.0.0.0', port=port, debug=False)

