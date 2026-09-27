"""Host tests for tools/wyze_cam — no camera, no docker-wyze-bridge."""
import io
import json
import pathlib
import sys
import tempfile
import unittest
import unittest.mock

_TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_TOOLS))

import fw  # noqa: E402
import wyze_cam as cam  # noqa: E402


def _cfg(**kw):
    base = {
            "camera_ip": "192.168.8.108",
        "bridge_url": "http://192.168.8.108:5000",
        "camera": "og-bench",
        "settle_ms": 0,
        "views": {
            "lcd": {"horizontal": 175, "vertical": 40, "why": "panel"},
            "overview": {"horizontal": 180, "vertical": 20, "why": "whole"},
        },
        "api_key": "test-key",
    }
    base.update(kw)
    return base


class FakeHTTP:
    def __init__(self):
        self.calls = []
        self.handlers = []

    def add(self, method, path_substr, status, body, ctype="application/json"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.handlers.append((method, path_substr, status, body, ctype))

    def __call__(self, method, url, headers, body):
        rec = {"method": method, "url": url, "headers": headers, "body": body}
        self.calls.append(rec)
        for m, sub, status, resp, ctype in self.handlers:
            if m == method and sub in url:
                return status, resp, ctype
        raise AssertionError(f"unexpected {method} {url}")


class TestConfig(unittest.TestCase):
    def test_default_camera_ip_is_the_bench_pan(self):
        cfg = cam.merge_config({}, {}, environ={})
        self.assertEqual(cfg["camera_ip"], "192.168.8.108")
        self.assertEqual(cfg["bridge_url"], "http://192.168.8.108:5000")

    def test_env_wins_over_file(self):
        cfg = cam.merge_config(
            {"bridge_url": "http://file:5000", "camera": "filecam"},
            {},
            environ={"WYZE_BRIDGE_URL": "http://env:9", "WYZE_CAM": "envcam"},
        )
        self.assertEqual(cfg["bridge_url"], "http://env:9")
        self.assertEqual(cfg["camera"], "envcam")

    def test_local_views_override_stock(self):
        cfg = cam.merge_config(
            {"views": {"lcd": {"horizontal": 1, "vertical": 2}}},
            {"views": {"lcd": {"horizontal": 9, "vertical": 8}}},
            environ={},
        )
        self.assertEqual(cfg["views"]["lcd"]["horizontal"], 9)

    def test_stock_views_file_has_the_six_board_frames(self):
        cfg = cam.load_config()
        for name in cam.STOCK_VIEWS:
            self.assertIn(name, cfg["views"])
            self.assertIn("horizontal", cfg["views"][name])
            self.assertIn("vertical", cfg["views"][name])


class TestView(unittest.TestCase):
    def test_unknown_view(self):
        with self.assertRaises(cam.WyzeCamError) as ctx:
            cam.view_angles(_cfg(), "ceiling")
        self.assertIn("lcd", str(ctx.exception))

    def test_angles(self):
        h, v, why = cam.view_angles(_cfg(), "lcd")
        self.assertEqual((h, v), (175, 40))
        self.assertEqual(why, "panel")


class TestBridge(unittest.TestCase):
    def test_look_ptz_then_snapshot(self):
        http = FakeHTTP()
        http.add("PUT", "/api/og-bench/ptz_position", 200,
                 '{"status":"ok"}')
        jpeg = b"\xff\xd8\xff\xd9"
        http.add("GET", "/snapshot/og-bench.jpg", 200, jpeg, "image/jpeg")
        slept = []
        cfg = _cfg(settle_ms=50)
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "lcd.jpg"
            rc = cam.main(
                ["look", "lcd", "-o", str(out)],
                cfg=cfg, transport=http, sleeper=slept.append,
            )
            self.assertEqual(rc, 0)
            self.assertTrue(out.exists())
            self.assertEqual(out.read_bytes(), jpeg)
        self.assertEqual(http.calls[0]["method"], "PUT")
        self.assertIn("ptz_position", http.calls[0]["url"])
        body = json.loads(http.calls[0]["body"])
        self.assertEqual(body, {"horizontal": 175, "vertical": 40})
        self.assertIn("snapshot/og-bench.jpg", http.calls[1]["url"])
        self.assertIn("api=test-key", http.calls[0]["url"])
        self.assertEqual(http.calls[0]["headers"]["api"], "test-key")
        self.assertEqual(slept, [0.05])

    def test_pan_left_sends_rotary_degree(self):
        http = FakeHTTP()
        http.add("PUT", "/rotary_degree", 200, '{"status":"ok"}')
        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            rc = cam.main(
                ["pan", "left", "--deg", "20"],
                cfg=_cfg(), transport=http, sleeper=lambda _s: None,
            )
        self.assertEqual(rc, 0)
        body = json.loads(http.calls[0]["body"])
        self.assertEqual(body["horizontal"], -20)
        self.assertEqual(body["vertical"], 0)
        self.assertEqual(body["speed"], 5)

    def test_pan_up(self):
        http = FakeHTTP()
        http.add("PUT", "/rotary_degree", 200, '{"status":"ok"}')
        cam.main(["nudge", "up", "--deg", "7", "--speed", "9"],
                 cfg=_cfg(), transport=http, sleeper=lambda _s: None)
        body = json.loads(http.calls[0]["body"])
        self.assertEqual(body, {"horizontal": 0, "vertical": 7, "speed": 9})

    def test_rejects_non_jpeg_snapshot(self):
        http = FakeHTTP()
        http.add("GET", "/snapshot/", 200, b"<html>nope</html>", "text/html")
        rc = cam.main(["snap"], cfg=_cfg(), transport=http,
                      sleeper=lambda _s: None)
        self.assertEqual(rc, 1)

    def test_go2rtc_jpeg_skips_tutk_snapshot(self):
        http = FakeHTTP()
        jpeg = b"\xff\xd8\xff\xd9"
        http.add("GET", "/api/frame.jpeg", 200, jpeg, "image/jpeg")
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "frame.jpg"
            buf = io.StringIO()
            with unittest.mock.patch("sys.stdout", buf):
                rc = cam.main(
                    ["snap", "-o", str(out)],
                    cfg=_cfg(go2rtc_url="http://go2rtc:1984"),
                    transport=http,
                    sleeper=lambda _s: None,
                )
            self.assertEqual(rc, 0)
            self.assertTrue(out.read_bytes().startswith(b"\xff\xd8"))
        self.assertTrue(any("/api/frame.jpeg" in c["url"] for c in http.calls))
        self.assertFalse(any("/snapshot/" in c["url"] for c in http.calls))

    def test_go2rtc_connection_error_falls_back_to_tutk(self):
        def boom(method, url, headers, body):
            if "1984" in url:
                raise cam.urllib.error.URLError("refused")
            return 200, b"<html>nope</html>", "text/html"

        rc = cam.main(
            ["snap"],
            cfg=_cfg(go2rtc_url="http://127.0.0.1:1984"),
            transport=boom,
            sleeper=lambda _s: None,
        )
        self.assertEqual(rc, 1)

    def test_401_mentions_api_key(self):
        http = FakeHTTP()
        http.add("GET", "/api", 401, b'{"error":"Unauthorized"}')
        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            rc = cam.main(["cameras"], cfg=_cfg(), transport=http)
        self.assertEqual(rc, 1)
        self.assertIn("WYZE_BRIDGE_API", buf.getvalue())

    def test_unreachable_tells_you_to_use_a_local_agent(self):
        def boom(_method, _url, _headers, _body):
            raise cam.urllib.error.URLError("timed out")

        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            rc = cam.main(["cameras"], cfg=_cfg(), transport=boom)
        self.assertEqual(rc, 1)
        out = buf.getvalue()
        self.assertIn("192.168.8.108", out)
        self.assertIn("agent worker start", out)

    def test_save_view_writes_local_json(self):
        http = FakeHTTP()
        http.add("GET", "/ptz_position", 200,
                 '{"horizontal": 171, "vertical": 33}')
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "config.local.json"
            cfg = _cfg(local_path=str(path))
            rc = cam.main(["save-view", "lcd"], cfg=cfg, transport=http,
                          sleeper=lambda _s: None)
            self.assertEqual(rc, 0)
            saved = json.loads(path.read_text())
        self.assertEqual(saved["views"]["lcd"]["horizontal"], 171)
        self.assertEqual(saved["camera_ip"], "192.168.8.108")

    def test_probe_reports_lan_miss(self):
        http = FakeHTTP()
        http.add("GET", "/api", 200, '{"cameras": {"og-bench": {}}}')

        def closed(_host, _port):
            return False

        # Bridge succeeds even if TCP probe of the camera IP is closed —
        # wyze-bridge may run on localhost while the cam is elsewhere.
        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            rc = cam.main(["probe"], cfg=_cfg(), transport=http, prober=closed)
        self.assertEqual(rc, 0)
        data = json.loads(buf.getvalue())
        self.assertTrue(data["bridge_ok"])
        self.assertTrue(data["on_lan"])
        self.assertEqual(data["cameras"], ["og-bench"])

    def test_probe_cloud_vm(self):
        def boom(_method, _url, _headers, _body):
            raise cam.urllib.error.URLError("timed out")

        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            rc = cam.main(
                ["probe"], cfg=_cfg(), transport=boom,
                prober=lambda *_a: False,
            )
        self.assertEqual(rc, 1)
        data = json.loads(buf.getvalue())
        self.assertFalse(data["on_lan"])
        self.assertIn("not on the bench LAN", data["hint"])


class TestWindowsWslFallback(unittest.TestCase):
    def test_loopback_dead_only_for_localhost(self):
        self.assertFalse(cam._windows_loopback_dead("http://192.168.8.108:5000"))
        self.assertTrue(cam._windows_loopback_dead("http://127.0.0.1:1"))

    def test_injected_transport_does_not_exec_wsl(self):
        http = FakeHTTP()
        http.add("GET", "/api", 200, '{"cameras":{"og-bench":{}}}')
        with unittest.mock.patch("wyze_cam.exec_via_wsl") as ex:
            rc = cam.main(["cameras"], cfg=_cfg(), transport=http)
        self.assertEqual(rc, 0)
        ex.assert_not_called()


class TestFwDispatch(unittest.TestCase):
    def test_print_pan_does_not_need_a_nickname(self):
        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            rc = cam.main(["--print", "pan", "left", "--deg", "15"],
                          cfg=_cfg(camera=""))
        self.assertEqual(rc, 0)
        lines = [json.loads(l) for l in buf.getvalue().splitlines() if l.strip()]
        printed = [o for o in lines if "print" in o][-1]
        self.assertIn("/api/CAM/rotary_degree", printed["print"][0]["url"])
        self.assertIn("-15", printed["print"][0]["body"])

    def test_fw_cam_passes_remainder(self):
        with unittest.mock.patch("wyze_cam.main", return_value=0) as m:
            rc = fw.main(["cam", "pan", "left", "--deg", "15"])
        self.assertEqual(rc, 0)
        m.assert_called_once_with(["pan", "left", "--deg", "15"])

    def test_fw_cam_print_look(self):
        with unittest.mock.patch("wyze_cam.main", return_value=0) as m:
            rc = fw.main(["cam", "--print", "look", "lcd"])
        self.assertEqual(rc, 0)
        m.assert_called_once_with(["--print", "look", "lcd"])


if __name__ == "__main__":
    unittest.main()
