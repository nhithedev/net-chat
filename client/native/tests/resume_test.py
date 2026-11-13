"""Test lõi mạng C (netchat-core) kết nối lại + RESUME, chạy với server thật.

Dùng: python3 -I tests/resume_test.py <netchat-core> <netchat-server>
Cần DB đang chạy (xem docker-compose.yml). Lõi nói chuyện qua pipe bằng khung
[uint32 BE length][uint8 kind][body] (xem đầu src/core.c).
"""
import json
import os
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

K_SEND, K_CTRL, K_RECV, K_STATUS = 0x01, 0x02, 0x81, 0x82
RUN_ID = "%05x" % (int(time.time() * 1000) & 0xFFFFF)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Server:
    def __init__(self, exe, port):
        self.exe, self.port = exe, port
        self.log = tempfile.TemporaryFile()
        self.proc = None

    def start(self):
        self.proc = subprocess.Popen([self.exe, str(self.port)], stdout=subprocess.DEVNULL,
                                     stderr=self.log)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", self.port), 0.2).close()
                return
            except OSError:
                time.sleep(0.05)
        raise RuntimeError("server không khởi động được")

    def stop(self):
        if self.proc:
            self.proc.terminate()
            self.proc.wait(5)
            self.proc = None


class Core:
    """Chạy netchat-core và gom khung nó gửi ra."""

    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL)
        self.buf = b""
        self.n = 0

    def write(self, kind, obj):
        body = json.dumps(obj).encode()
        self.p.stdin.write(struct.pack(">IB", 1 + len(body), kind) + body)
        self.p.stdin.flush()

    def ctrl(self, **kw):
        self.write(K_CTRL, kw)

    def request(self, type_, data=None):
        self.n += 1
        rid = f"t-{self.n}"
        msg = {"type": type_, "id": rid}
        if data is not None:
            msg["data"] = data
        self.write(K_SEND, msg)
        return rid

    def read(self, timeout):
        """Một khung (kind, obj), hoặc None nếu hết giờ."""
        end = time.monotonic() + timeout
        while True:
            if len(self.buf) >= 4:
                (l,) = struct.unpack(">I", self.buf[:4])
                if len(self.buf) >= 4 + l:
                    kind, body = self.buf[4], self.buf[5:4 + l]
                    self.buf = self.buf[4 + l:]
                    return kind, json.loads(body)
            left = end - time.monotonic()
            if left <= 0:
                return None
            if select.select([self.p.stdout], [], [], left)[0]:
                chunk = os.read(self.p.stdout.fileno(), 65536)
                if not chunk:
                    return None
                self.buf += chunk

    def wait_status(self, state, timeout=10):
        end = time.monotonic() + timeout
        seen = []
        while time.monotonic() < end:
            f = self.read(end - time.monotonic())
            if f is None:
                break
            if f[0] == K_STATUS:
                seen.append(f[1]["state"])
                if f[1]["state"] == state:
                    return f[1], seen
        self.fail_states = seen
        return None, seen

    def wait_response(self, rid, timeout=5):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            f = self.read(end - time.monotonic())
            if f and f[0] == K_RECV and f[1].get("id") == rid:
                return f[1]
        return None

    def close(self):
        try:
            self.p.stdin.close()
            self.p.wait(5)
        except Exception:
            self.p.kill()


class ResumeTest(unittest.TestCase):
    def setUp(self):
        self.srv = Server(SERVER, free_port())
        self.srv.start()
        self.addCleanup(self.srv.stop)
        self.core = Core(CORE)
        self.addCleanup(self.core.close)

    def login(self, user):
        c = self.core
        c.ctrl(cmd="connect", host="127.0.0.1", port=self.srv.port)
        st, _ = c.wait_status("open")
        self.assertIsNotNone(st, "không mở được kết nối")
        c.request("REGISTER", {"username": user, "password": "123456", "display_name": "T"})
        r = c.wait_response("t-1")
        self.assertTrue(r and r["ok"], r)
        rid = c.request("LOGIN", {"username": user, "password": "123456"})
        r = c.wait_response(rid)
        self.assertTrue(r and r["ok"], r)
        return r["data"]["token"]

    def restart_server(self, down=1.0):
        self.srv.stop()
        time.sleep(down)
        self.srv.start()

    def test_reconnect_after_server_restart(self):
        user = f"core_{RUN_ID}"
        token = self.login(user)
        self.core.ctrl(cmd="set_session", token=token)
        self.restart_server(down=1.0)           # server chết rồi sống lại trên cùng cổng
        st, seen = self.core.wait_status("restored", 15)
        self.assertIsNotNone(st, f"không restored; đã thấy {seen}")
        self.assertIn("reconnecting", seen)
        st, _ = self.core.wait_status("open", 5)
        self.assertIsNotNone(st)
        # đã xác thực lại thật: ONLINE_LIST không bị ERR_NOT_AUTHENTICATED
        rid = self.core.request("ONLINE_LIST")
        r = self.core.wait_response(rid)
        self.assertTrue(r and r["ok"], r)
        names = [u["username"] for u in r["data"]["users"]]
        self.assertIn(user, names)

    def test_invalid_token_gives_closed(self):
        token = self.login(f"core_bad_{RUN_ID}")
        self.core.ctrl(cmd="set_session", token="0" * 64)    # token không tồn tại
        self.restart_server(down=0.3)
        st, seen = self.core.wait_status("closed", 15)
        self.assertIsNotNone(st, f"không closed; đã thấy {seen}")
        self.assertEqual(st["reason"], "token_invalid")
        self.assertNotIn("restored", seen)
        # không còn thử lại nữa
        self.assertIsNone(self.core.read(2))

    def test_no_token_means_no_reconnect(self):
        self.login(f"core_nt_{RUN_ID}")                      # không set_session
        self.restart_server(down=0.3)
        st, seen = self.core.wait_status("closed", 5)
        self.assertIsNotNone(st)
        self.assertNotIn("reconnecting", seen)

    def test_close_cmd_clears_token(self):
        token = self.login(f"core_cl_{RUN_ID}")
        self.core.ctrl(cmd="set_session", token=token)
        self.core.ctrl(cmd="close")
        st, seen = self.core.wait_status("closed", 5)
        self.assertEqual(st["reason"], "client_closed")
        self.assertIsNone(self.core.read(2))                 # không tự vào lại

    def test_gives_up_when_server_stays_down(self):
        token = self.login(f"core_gu_{RUN_ID}")
        self.core.ctrl(cmd="set_session", token=token)
        self.srv.stop()                                      # không bật lại
        st, seen = self.core.wait_status("reconnecting", 5)
        self.assertIsNotNone(st, seen)
        st, seen = self.core.wait_status("closed", 70)
        self.assertIsNotNone(st, seen)
        self.assertEqual(st["reason"], "resume_timeout")

    def test_kicked_does_not_reconnect(self):
        user = f"core_k_{RUN_ID}"
        token = self.login(user)
        self.core.ctrl(cmd="set_session", token=token)
        # đăng nhập từ nơi khác -> lõi nhận EVT_KICKED rồi bị đóng
        other = Core(CORE)
        self.addCleanup(other.close)
        other.ctrl(cmd="connect", host="127.0.0.1", port=self.srv.port)
        other.wait_status("open")
        rid = other.request("LOGIN", {"username": user, "password": "123456"})
        self.assertTrue(other.wait_response(rid)["ok"])
        st, seen = self.core.wait_status("closed", 5)
        self.assertIsNotNone(st, seen)
        self.assertNotIn("reconnecting", seen)


if __name__ == "__main__":
    CORE, SERVER = sys.argv.pop(1), sys.argv.pop(1)
    unittest.main(verbosity=2)
