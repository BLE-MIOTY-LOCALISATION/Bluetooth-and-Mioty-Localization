import os
import sys

# Setup path to sibling GoogleFindMyTools
CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
MIOTY_BLE_TESTS_DIR = os.path.dirname(os.path.dirname(CURRENT_DIR))
GFMT_DIR = os.path.join(MIOTY_BLE_TESTS_DIR, "GoogleFindMyTools")
if os.path.exists(GFMT_DIR):
    sys.path.insert(0, GFMT_DIR)
else:
    sys.path.insert(0, r"c:\Users_windows\Chandu B Reddy\Projects\LOcalee\Board-standalone-bring-up\MIOTY_BLE_Tests\GoogleFindMyTools")

from NovaApi.ListDevices.nbe_list_devices import request_device_list
from ProtoDecoders.decoder import parse_device_list_protobuf, get_canonic_ids
from SpotApi.UploadPrecomputedPublicKeyIds.upload_precomputed_public_key_ids import refresh_custom_trackers
from fmdn_client import query_location_for_device

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
            
            # Map device name to canonic_id
            canonic_ids = get_canonic_ids(device_list)
            self.devices = {name: canonic_id for name, canonic_id in canonic_ids}
            print(f"[BLELocationService] Loaded devices: {list(self.devices.keys())}")
        except Exception as e:
            print(f"[BLELocationService] Failed to refresh devices: {e}")

    def get_location(self, device_name, history=False, force_refresh=False):
        """Fetches location reports for a given device name."""
        # Try refreshing if force_refresh is requested or not found in cache
        if force_refresh or device_name not in self.devices:
            self.refresh_devices()

        if device_name not in self.devices:
            raise KeyError(f"Device '{device_name}' not found in the Google Find My Device account.")

        canonic_id = self.devices[device_name]
        
        # Retrieve the location reports list (decrypted)
        locations = query_location_for_device(canonic_id, device_name)
        
        if not locations:
            return {
                "source": "ble",
                "device_name": device_name,
                "canonic_id": canonic_id,
                "status": "No location reports found"
            }
            
        if history:
            # Format and sort history by timestamp descending (latest first)
            formatted_locations = []
            for loc in locations:
                report = {
                    "type": loc.get("type"),
                    "timestamp": loc.get("timestamp"),
                    "timestamp_readable": loc.get("timestamp_readable"),
                    "accuracy_meters": loc.get("accuracy_meters"),
                    "status": loc.get("status"),
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
                
            formatted_locations.sort(key=lambda x: x.get("timestamp", 0), reverse=True)
            
            return {
                "source": "ble",
                "device_name": device_name,
                "canonic_id": canonic_id,
                "history": formatted_locations
            }
        else:
            # Select the latest location based on timestamp
            latest_loc = max(locations, key=lambda x: x.get("timestamp", 0))
            
            # Construct the response dictionary
            response = {
                "source": "ble",
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
