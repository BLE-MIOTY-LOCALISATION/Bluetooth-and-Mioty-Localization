import os
import sys

# Setup path to sibling GoogleFindMyTools -- vendored two levels up
# (repo_root/GoogleFindMyTools), a sibling of 7_ble_mioty_FMDN_tracker,
# not of location_api itself.
CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(CURRENT_DIR))
GFMT_DIR = os.path.join(REPO_ROOT, "GoogleFindMyTools")
if os.path.exists(GFMT_DIR):
    sys.path.insert(0, GFMT_DIR)
else:
    raise FileNotFoundError(
        f"GoogleFindMyTools not found at {GFMT_DIR} -- expected as a sibling "
        f"of 7_ble_mioty_FMDN_tracker at the repo root. See FMDN_tracker.md."
    )

from NovaApi.ListDevices.nbe_list_devices import request_device_list
from ProtoDecoders.decoder import parse_device_list_protobuf, get_canonic_ids
from SpotApi.UploadPrecomputedPublicKeyIds.upload_precomputed_public_key_ids import refresh_custom_trackers
from fmdn_client import query_location_for_device

import json
import base64
import requests
from datetime import datetime

from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.x963kdf import X963KDF
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend

# Device names to exclude from this dashboard entirely -- e.g. personal
# phones/tablets that also happen to be registered to the same Google
# account as the tracker hardware. Filtered out at the source here so they
# never reach the API or frontend at all (not just hidden in the UI),
# which matters for demos/screenshots/recordings where the raw network
# response could otherwise expose them. Add any device name shown in the
# dashboard that isn't project hardware.
HIDDEN_DEVICE_NAMES = {
    "Reno 11",
    "Galaxy Tab S9 FE",
}

class BLELocationService:
    def __init__(self):
        self.devices = {}
        self.refresh_devices()

    def refresh_devices(self):
        """Fetches the latest registered devices list from Google Find My Device."""
        print("[BLELocationService] Refreshing device list from Google...")
        try:
            result_hex = request_device_list()
            device_list = parse_device_list_protobuf(result_hex)

            # Refresh the tracker (handles the 4-day announcement refresh)
            refresh_custom_trackers(device_list)

            # Map device name to canonic_id, excluding anything in HIDDEN_DEVICE_NAMES
            canonic_ids = get_canonic_ids(device_list)
            self.devices = {
                name: canonic_id
                for name, canonic_id in canonic_ids
                if name not in HIDDEN_DEVICE_NAMES
            }
            print(f"[BLELocationService] Loaded devices: {list(self.devices.keys())}")
        except Exception as e:
            print(f"[BLELocationService] Failed to refresh devices: {e}")

    def fetch_apple_locations(self, device_name="BLE MIOTY TRACKER I"):
        """Fetches and decrypts Apple Find My locations from macless-haystack endpoint for a specific tracker."""
        apple_reports = []
        try:
            # 1. Read the Apple keys
            project_root = os.path.dirname(REPO_ROOT) # Resolves to Bluetooth-and-Mioty-Localization
            keys_path = os.path.join(project_root, "macless-haystack", "endpoint", "data", "keys.json")
            if not os.path.exists(keys_path):
                print(f"[BLELocationService] No Apple keys found at {keys_path}")
                return apple_reports
                
            with open(keys_path, 'r') as f:
                keys_data = json.load(f)

            private_key_b64 = None
            if isinstance(keys_data, list):
                # Search by device name or fallback to index
                for k in keys_data:
                    if k.get("name") == device_name:
                        private_key_b64 = k.get("privateKey") or k.get("private_key")
                        break
                if not private_key_b64 and len(keys_data) > 0:
                    idx = 1 if device_name == "BLE MIOTY TRACKER II" and len(keys_data) > 1 else 0
                    private_key_b64 = keys_data[idx].get('privateKey') or keys_data[idx].get('private_key')
            elif isinstance(keys_data, dict):
                private_key_b64 = keys_data.get('private_key') or keys_data.get('privateKey')
            
            if not private_key_b64:
                return apple_reports

            # Derive hashed advertisement key from private key
            import hashlib
            priv_bytes = base64.b64decode(private_key_b64)
            priv_key = ec.derive_private_key(int.from_bytes(priv_bytes, 'big'), ec.SECP224R1(), default_backend())
            pub_x = priv_key.public_key().public_numbers().x
            adv_bytes = pub_x.to_bytes(28, 'big')
            hashed_adv_key = base64.b64encode(hashlib.sha256(adv_bytes).digest()).decode('ascii')
                
            # 2. Query mh_endpoint running locally
            payload = {
                "days": 7,
                "ids": [hashed_adv_key]
            }
            try:
                # Use a short timeout so we don't block forever if it's down
                resp = requests.post("http://127.0.0.1:6176/", json=payload, timeout=5)
                if resp.status_code != 200:
                    print(f"[BLELocationService] Apple endpoint returned {resp.status_code}")
                    return apple_reports
                data = resp.json()
            except requests.exceptions.RequestException as e:
                print(f"[BLELocationService] Failed to reach Apple endpoint: {e}")
                return apple_reports
                
            # 3. Decrypt results
            results = data.get("results", [])
            for entry in results:
                try:
                    payload_b64 = entry.get('payload')
                    raw = bytearray(base64.b64decode(payload_b64))
                    if len(raw) > 88:
                        raw = raw[:4] + raw[5:]
                    
                    timestamp = int.from_bytes(raw[0:4], 'big') + 978307200
                    confidence = raw[4]
                    eph_key_bytes = bytes(raw[5:62])
                    enc_data = bytes(raw[62:72])
                    mac = bytes(raw[72:88])
                    
                    # ECDH
                    eph_key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP224R1(), eph_key_bytes)
                    shared_key = priv_key.exchange(ec.ECDH(), eph_key)
                    
                    # KDF
                    kdf = X963KDF(
                        algorithm=hashes.SHA256(),
                        length=32,
                        sharedinfo=eph_key_bytes,
                        backend=default_backend()
                    )
                    derived = kdf.derive(shared_key)
                    sym_key = derived[:16]
                    iv = derived[16:]
                    
                    # AES-GCM decryption
                    decryptor = Cipher(algorithms.AES(sym_key), modes.GCM(iv, mac), default_backend()).decryptor()
                    decrypted = decryptor.update(enc_data) + decryptor.finalize()
                    
                    lat = int.from_bytes(decrypted[0:4], 'big', signed=True) / 10000000.0
                    lon = int.from_bytes(decrypted[4:8], 'big', signed=True) / 10000000.0
                    status = decrypted[9]
                    
                    # Format standard report for frontend
                    dt = datetime.fromtimestamp(timestamp)
                    apple_reports.append({
                        "source": "apple",
                        "type": "geo",
                        "timestamp": timestamp,
                        "timestamp_readable": dt.strftime('%Y-%m-%d %H:%M:%S'),
                        "latitude": lat,
                        "longitude": lon,
                        "accuracy_meters": confidence,
                        "status": "Apple Network",
                        "is_own_report": False,
                        "google_maps_link": f"https://www.google.com/maps/search/?api=1&query={lat},{lon}"
                    })
                except Exception as e:
                    print(f"[BLELocationService] Failed to decrypt Apple payload: {e}")
                    
            return apple_reports
        except Exception as e:
            print(f"[BLELocationService] Error fetching Apple locations: {e}")
            return apple_reports

    def get_location(self, device_name, history=False, force_refresh=False):
        """Fetches location reports for a given device name."""
        # Try refreshing if force_refresh is requested or not found in cache
        if force_refresh or device_name not in self.devices:
            self.refresh_devices()

        if device_name not in self.devices:
            raise KeyError(f"Device '{device_name}' not found in the Google Find My Device account.")

        canonic_id = self.devices[device_name]
        
        # Retrieve the location reports list (decrypted)
        locations = []
        try:
            locations = query_location_for_device(canonic_id, device_name)
        except Exception as e:
            print(f"[BLELocationService] Failed to fetch Google locations: {e}")
            
        # Format google locations and add source
        formatted_locations = []
        if locations:
            for loc in locations:
                report = {
                    "source": "google",
                    "type": loc.get("type"),
                    "timestamp": loc.get("timestamp"),
                    "timestamp_readable": loc.get("timestamp_readable"),
                    "accuracy_meters": loc.get("accuracy_meters"),
                    "status": loc.get("status", "Google Network"),
                    "is_own_report": loc.get("is_own_report")
                }
                if loc.get("type") == "geo":
                    report.update({
                        "latitude": loc.get("latitude"),
                        "longitude": loc.get("longitude"),
                        "altitude": loc.get("altitude"),
                        "google_maps_link": f"https://www.google.com/maps/search/?api=1&query={loc.get('latitude')},{loc.get('longitude')}"
                    })
                elif loc.get("type") == "semantic":
                    report.update({
                        "name": loc.get("name")
                    })
                formatted_locations.append(report)
        
        # Merge Apple locations for hardware trackers broadcasting an Apple Find My key
        if device_name in ("BLE MIOTY TRACKER I", "BLE MIOTY TRACKER II", "BLE MIOTY TRACKER III"):
            apple_reports = self.fetch_apple_locations(device_name)
            formatted_locations.extend(apple_reports)
        
        if not formatted_locations:
            return {
                "source": "ble",
                "device_name": device_name,
                "canonic_id": canonic_id,
                "status": "No location reports found"
            }
            
        if history:
            formatted_locations.sort(key=lambda x: x.get("timestamp", 0), reverse=True)
            return {
                "source": "ble",
                "device_name": device_name,
                "canonic_id": canonic_id,
                "history": formatted_locations
            }
        else:
            # Select the latest location based on timestamp
            latest_loc = max(formatted_locations, key=lambda x: x.get("timestamp", 0))
            
            # Construct the response dictionary
            response = {
                "source": latest_loc.get("source"),
                "device_name": device_name,
                "canonic_id": canonic_id,
                "type": latest_loc.get("type"),
                "timestamp": latest_loc.get("timestamp"),
                "timestamp_readable": latest_loc.get("timestamp_readable"),
                "accuracy_meters": latest_loc.get("accuracy_meters"),
                "status": latest_loc.get("status"),
                "is_own_report": latest_loc.get("is_own_report")
            }
            
            if latest_loc.get("type") == "geo":
                response.update({
                    "latitude": latest_loc.get("latitude"),
                    "longitude": latest_loc.get("longitude"),
                    "altitude": latest_loc.get("altitude"),
                    "google_maps_link": f"https://www.google.com/maps/search/?api=1&query={latest_loc.get('latitude')},{latest_loc.get('longitude')}"
                })
            elif latest_loc.get("type") == "semantic":
                response.update({
                    "name": latest_loc.get("name")
                })
                
            return response
