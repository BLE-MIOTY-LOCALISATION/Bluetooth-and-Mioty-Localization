import os
import sys
import time
import hashlib
import datetime

# Setup path to sibling GoogleFindMyTools
CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
MIOTY_BLE_TESTS_DIR = os.path.dirname(os.path.dirname(CURRENT_DIR))
GFMT_DIR = os.path.join(MIOTY_BLE_TESTS_DIR, "GoogleFindMyTools")
if os.path.exists(GFMT_DIR):
    sys.path.insert(0, GFMT_DIR)
else:
    sys.path.insert(0, r"c:\Users_windows\Chandu B Reddy\Projects\LOcalee\Board-standalone-bring-up\MIOTY_BLE_Tests\GoogleFindMyTools")

# Import protobuf and crypto classes from GoogleFindMyTools
from google.protobuf import text_format
from Auth.fcm_receiver import FcmReceiver
from NovaApi.ExecuteAction.LocateTracker.decrypt_locations import (
    retrieve_identity_key, is_mcu_tracker
)
from KeyBackup.cloud_key_decryptor import decrypt_aes_gcm
from FMDNCrypto.foreign_tracker_cryptor import decrypt
from NovaApi.ExecuteAction.nbe_execute_action import create_action_request, serialize_action_request
from NovaApi.nova_request import nova_request
from NovaApi.scopes import NOVA_ACTION_API_SCOPE
from NovaApi.util import generate_random_uuid
from ProtoDecoders import DeviceUpdate_pb2, Common_pb2
from ProtoDecoders.decoder import parse_device_update_protobuf

def create_location_request_payload(canonic_device_id, fcm_registration_id, request_uuid):
    action_request = create_action_request(canonic_device_id, fcm_registration_id, request_uuid=request_uuid)
    
    # Static parameters matching the GoogleFindMyTools default implementation
    action_request.action.locateTracker.lastHighTrafficEnablingTime.seconds = 1732120060
    action_request.action.locateTracker.contributorType = DeviceUpdate_pb2.SpotContributorType.FMDN_ALL_LOCATIONS

    return serialize_action_request(action_request)

def decrypt_device_update_locations(device_update_protobuf):
    """Decrypts location reports from a DeviceUpdate protobuf message and returns a list of dicts."""
    device_registration = device_update_protobuf.deviceMetadata.information.deviceRegistration
    identity_key = retrieve_identity_key(device_registration)
    locations_proto = device_update_protobuf.deviceMetadata.information.locationInformation.reports.recentLocationAndNetworkLocations
    is_mcu = is_mcu_tracker(device_registration)

    recent_location = locations_proto.recentLocation
    recent_location_time = locations_proto.recentLocationTimestamp

    network_locations = list(locations_proto.networkLocations)
    network_locations_time = list(locations_proto.networkLocationTimestamps)

    if locations_proto.HasField("recentLocation"):
        network_locations.append(recent_location)
        network_locations_time.append(recent_location_time)

    decrypted_list = []
    for loc, time_val in zip(network_locations, network_locations_time):
        timestamp = int(time_val.seconds)
        ts_readable = datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S') if timestamp else "N/A"
        
        if loc.status == Common_pb2.Status.SEMANTIC:
            decrypted_list.append({
                "type": "semantic",
                "name": loc.semanticLocation.locationName,
                "timestamp": timestamp,
                "timestamp_readable": ts_readable,
                "accuracy_meters": 0,
                "status": "SEMANTIC",
                "is_own_report": True
            })
        else:
            encrypted_location = loc.geoLocation.encryptedReport.encryptedLocation
            public_key_random = loc.geoLocation.encryptedReport.publicKeyRandom

            # Decrypt the encrypted location report
            if public_key_random == b"":  # Own Report
                identity_key_hash = hashlib.sha256(identity_key).digest()
                decrypted_loc_bytes = decrypt_aes_gcm(identity_key_hash, encrypted_location)
            else:
                time_offset = 0 if is_mcu else loc.geoLocation.deviceTimeOffset
                decrypted_loc_bytes = decrypt(identity_key, encrypted_location, public_key_random, time_offset)

            # Parse the decrypted payload
            proto_loc = DeviceUpdate_pb2.Location()
            proto_loc.ParseFromString(decrypted_loc_bytes)

            latitude = proto_loc.latitude / 1e7
            longitude = proto_loc.longitude / 1e7
            altitude = proto_loc.altitude

            # Status mapping from protobuf enum
            try:
                status_str = Common_pb2.Status.Name(loc.status)
            except ValueError:
                status_str = "UNKNOWN"

            decrypted_list.append({
                "type": "geo",
                "latitude": latitude,
                "longitude": longitude,
                "altitude": altitude,
                "timestamp": timestamp,
                "timestamp_readable": ts_readable,
                "accuracy_meters": loc.geoLocation.accuracy,
                "status": status_str,
                "is_own_report": bool(loc.geoLocation.encryptedReport.isOwnReport)
            })
            
    return decrypted_list

def query_location_for_device(canonic_device_id, name, timeout_seconds=15):
    """
    Queries location from Google and waits synchronously for the FCM push notification response.
    Cleans up callbacks to prevent memory leakage.
    """
    print(f"[FMDNClient] Requesting location data for {name} ({canonic_device_id})...")

    result_payload = None
    request_uuid = generate_random_uuid()

    # The FCM push notification callback
    def handle_location_response(response_hex):
        nonlocal result_payload
        try:
            device_update = parse_device_update_protobuf(response_hex)
            if device_update.fcmMetadata.requestUuid == request_uuid:
                print(f"[FMDNClient] FCM response received for request {request_uuid}")
                result_payload = device_update
        except Exception as err:
            print(f"[FMDNClient] Error parsing incoming FCM response: {err}")

    # Register callback on FCMReceiver singleton directly
    receiver = FcmReceiver()
    if not receiver._listening:
        receiver._start_listener_in_background()
        
    fcm_token = receiver.credentials['fcm']['registration']['token']
    receiver.location_update_callbacks.append(handle_location_response)

    try:
        # Build payload and send request to Google Spot/Locate API
        hex_payload = create_location_request_payload(canonic_device_id, fcm_token, request_uuid)
        nova_request(NOVA_ACTION_API_SCOPE, hex_payload)

        # Wait for the response callback with a timeout
        start_time = time.time()
        while result_payload is None:
            if time.time() - start_time > timeout_seconds:
                raise TimeoutError(f"FMDN location request timed out after {timeout_seconds} seconds.")
            time.sleep(0.1)

        # Decrypt locations
        return decrypt_device_update_locations(result_payload)
        
    finally:
        # Ensure we always clean up the callback to prevent memory leaks
        if handle_location_response in receiver.location_update_callbacks:
            receiver.location_update_callbacks.remove(handle_location_response)
            print("[FMDNClient] Unregistered FCM callback successfully.")
