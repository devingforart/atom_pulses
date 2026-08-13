"""Pure request de-duplication for the PULSO Live bridge.

Kept separate from Live's ControlSurface module so the startup contract can be
unit-tested without launching Ableton.
"""

import json
import os


def read_request_id(path):
    if not os.path.isfile(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as source:
            value = json.load(source).get("request_id")
        return str(value) if value else None
    except (OSError, ValueError, AttributeError):
        return None


class RequestGuard(object):
    """Adopts an existing command at startup and claims each later UUID once."""

    def __init__(self, request_path):
        self._last_request_id = read_request_id(request_path)

    @property
    def adopted_request_id(self):
        return self._last_request_id

    def claim(self, request):
        request_id = request.get("request_id") if isinstance(request, dict) else None
        if not request_id:
            return False
        request_id = str(request_id)
        if request_id == self._last_request_id:
            return False
        self._last_request_id = request_id
        return True
