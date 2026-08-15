import sys
import os

# Add the current directory to the Python path so it can find the modules
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from NovaApi.ListDevices.nbe_list_devices import request_device_list
from ProtoDecoders.decoder import parse_device_list_protobuf, get_canonic_ids
from SpotApi.UploadPrecomputedPublicKeyIds.upload_precomputed_public_key_ids import refresh_custom_trackers
from NovaApi.ExecuteAction.LocateTracker.location_request import get_location_data_for_device

def auto_locate():
    print("Authenticating and fetching device list...")
    result_hex = request_device_list()
    device_list = parse_device_list_protobuf(result_hex)
    
    # Refresh the tracker (the 4-day announcement)
    refresh_custom_trackers(device_list)
    canonic_ids = get_canonic_ids(device_list)

    # Search for our specific custom tracker
    found = False
    for device_name, canonic_id in canonic_ids:
        if device_name == "GoogleFindMyTools \u00b5C":
            print(f"\nFound tracker: {device_name} ({canonic_id})")
            print("Fetching and decrypting location...")
            print("-" * 50)
            
            # This handles the actual fetch, decrypt, and print
            get_location_data_for_device(canonic_id, device_name)
            found = True
            break
            
    if not found:
        print("Could not find a device named 'GoogleFindMyTools \u00b5C' in your account.")

if __name__ == '__main__':
    auto_locate()
