#
#  GoogleFindMyTools - A set of tools to interact with the Google Find My API
#  Copyright © 2024 Leon Böttger. All rights reserved.
#

import gpsoauth

from Auth.aas_token_retrieval import get_aas_token, invalidate_aas_token
from Auth.fcm_receiver import FcmReceiver


def _perform_oauth(username, aas_token, android_id, scope, request_app):
    return gpsoauth.perform_oauth(
        username, aas_token, android_id,
        service='oauth2:https://www.googleapis.com/auth/' + scope,
        app=request_app,
        client_sig='38918a453d07199354f8b19af05ec6562ced5788')


def request_token(username, scope, play_services = False):

    aas_token = get_aas_token()
    android_id = FcmReceiver().get_android_id()
    request_app = 'com.google.android.gms' if play_services else 'com.google.android.apps.adm'

    auth_response = _perform_oauth(username, aas_token, android_id, scope, request_app)

    if 'Auth' not in auth_response:
        # Cached AAS token was rejected (expired/revoked) - force a fresh interactive
        # login and retry once instead of failing outright.
        print(f"[TokenRetrieval] Cached AAS token rejected ({auth_response.get('Error', 'unknown error')}). Re-authenticating...")
        aas_token = invalidate_aas_token()
        auth_response = _perform_oauth(username, aas_token, android_id, scope, request_app)

    return auth_response['Auth']