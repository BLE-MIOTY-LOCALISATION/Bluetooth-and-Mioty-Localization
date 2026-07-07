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
    """Serves the Leaflet map dashboard index.html."""
    return app.send_static_file('index.html')

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
        return jsonify({
            "status": "error",
            "message": str(e)
        }), 504
    except Exception as e:
        return jsonify({
            "status": "error",
            "message": str(e)
        }), 500

if __name__ == '__main__':
    port = int(os.environ.get("PORT", 5000))
    app.run(host='0.0.0.0', port=port, debug=False)
