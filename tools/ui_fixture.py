"""Local-only UI fixture, NOT a camera emulator. Run with Python 3."""
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

PAGE = Path(__file__).resolve().parents[1] / "Esp32_Files/ESP_IDF_Project/main/index.html"
state = dict(preset_count=5, active_profile=0, preset_en=1, scene_mode=5,
             contrast=100, edge_enhancment_gear=1, detail_enhancement_gear=55,
             breathing=0, flip_mode=0, zoom=11, zoom_x=128, zoom_y=96,
             sensor_width=256, sensor_height=192, settings_saved=1,
             camera_tx_ok=1, camera_reply=1, flip_verified=1, nvs_writable=1)
state['pseudo_color'] = 0
state['backup'] = [1,0,0,11,128,96] + [1,0,5,0,100,1,55,1,1,0] + [1,0,9,0,100,2,55,1,1,0] + [0,0,9,0,50,1,50,1,1,0] * 3


def sync_backup():
    state['backup'][1:6] = [state[k] for k in ('active_profile','flip_mode','zoom','zoom_x','zoom_y')]
    offset = 6 + state['active_profile'] * 10
    for i, key in enumerate(('preset_en','pseudo_color','scene_mode','brightness','contrast','edge_enhancment_gear','detail_enhancement_gear','burn_protection_en','auto_shutter_en','breathing')):
        if key in state:
            state['backup'][offset+i] = state[key]


def load_preset():
    offset = 6 + state['active_profile'] * 10
    for i, key in enumerate(('preset_en','pseudo_color','scene_mode','brightness','contrast','edge_enhancment_gear','detail_enhancement_gear','burn_protection_en','auto_shutter_en','breathing')):
        state[key] = state['backup'][offset+i]


class Handler(BaseHTTPRequestHandler):
    def reply(self, data, code=200, content_type="application/json"):
        payload = data if isinstance(data, bytes) else json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path == "/get":
            self.reply(state)
        elif self.path == "/":
            self.reply(PAGE.read_bytes(), content_type="text/html; charset=utf-8")
        elif self.path == "/debug":
            self.reply(PAGE.with_name('debug.html').read_bytes(), content_type="text/html; charset=utf-8")
        else:
            self.reply({"message": "Not found"}, 404)

    def do_POST(self):
        data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if "zoom" in data:
            zoom = data["zoom"]
            if not (116 <= zoom["x"] <= 140 and 87 <= zoom["y"] <= 105):
                return self.reply({"message": "Alignment limit reached"}, 400)
            state.update(zoom_x=zoom["x"], zoom_y=zoom["y"], zoom=zoom["zoom"], flip_mode=0)
        elif 'restore' in data:
            state['backup'] = data['restore']
            for key, value in zip(('active_profile','flip_mode','zoom','zoom_x','zoom_y'),state['backup'][1:6]):
                state[key] = value
            load_preset()
        elif 'detail_preset' in data:
            slot = data['detail_preset']
            if state['backup'][6+slot*10]:
                return self.reply({'message':'Slot enabled'},400)
            state['backup'][6+slot*10:16+slot*10] = [0,0,3,0,50,0,55,1,1,0]
            state['active_profile'] = slot
            load_preset()
        else:
            state.update(data)
            if 'active_profile' in data:
                load_preset()
        sync_backup()
        self.reply({"saved": True, "camera_tx_ok": True, "message": "Fixture saved"})


if __name__ == "__main__":
    print("UI test fixture only: http://127.0.0.1:8765", flush=True)
    HTTPServer(("127.0.0.1", 8765), Handler).serve_forever()
