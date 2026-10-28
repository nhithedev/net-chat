"""Test tích hợp: chạy server thật, nói chuyện bằng socket TCP thô.

Dùng: python3 -I tests/integration.py <server> [<server-timeout-ngắn>]
Mỗi test tự mở kết nối riêng, tự kiểm tra. Server được chạy trên cổng trống.
"""
import json
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

RUN_ID = "%05x" % (int(time.time() * 1000) & 0xFFFFF)


def uname(base):
    """Username duy nhất mỗi lần chạy (user nằm lại trong DB); tối đa 20 ký tự."""
    return (base + "_" + RUN_ID)[:20]


SERVER = None
SERVER_FAST = None


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Server:
    def __init__(self, exe):
        self.port = free_port()
        # Log ra file (không phải pipe): pipe đầy sẽ làm server bị chặn ở fprintf.
        self.logfile = tempfile.TemporaryFile()
        self.proc = subprocess.Popen([exe, str(self.port)],
                                     stdout=subprocess.DEVNULL,
                                     stderr=self.logfile)
        for _ in range(100):                      # chờ server nghe cổng
            try:
                socket.create_connection(("127.0.0.1", self.port), 0.2).close()
                return
            except OSError:
                time.sleep(0.05)
        raise RuntimeError("server không khởi động được")

    def stop(self):
        self.proc.terminate()
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.logfile.seek(0)
        self.log = self.logfile.read().decode()
        self.logfile.close()
        # Tắt bằng SIGTERM phải thoát sạch (mã 0) và không có báo cáo sanitizer.
        problems = [l for l in self.log.splitlines()
                    if "Sanitizer" in l or "runtime error" in l or "SUMMARY" in l]
        if self.proc.returncode != 0 or problems:
            raise AssertionError(
                f"server thoát mã {self.proc.returncode}; log:\n"
                + "\n".join(problems[:10] or self.log.splitlines()[-10:]))


def frame(obj):
    data = obj if isinstance(obj, bytes) else json.dumps(
        obj, ensure_ascii=False, separators=(",", ":")).encode()
    return struct.pack(">I", len(data)) + data


class Client:
    def __init__(self, port, rcvbuf=None):
        self.sock = socket.socket()
        if rcvbuf:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
        self.sock.settimeout(5)
        self.sock.connect(("127.0.0.1", port))
        self.buf = b""

    def send(self, data):
        self.sock.sendall(data)

    def send_msg(self, obj):
        self.send(frame(obj))

    def _pop(self):
        """Lấy 1 message đã có đủ trong buffer, không thì None."""
        if len(self.buf) < 4:
            return None
        (n,) = struct.unpack(">I", self.buf[:4])
        if len(self.buf) < 4 + n:
            return None
        payload, self.buf = self.buf[4:4 + n], self.buf[4 + n:]
        return json.loads(payload.decode())

    def recv_msg(self):
        """Một message JSON; None nếu server đã đóng (EOF sạch)."""
        while True:
            m = self._pop()
            if m is not None:
                return m
            chunk = self.sock.recv(65536)
            if not chunk:
                return None
            self.buf += chunk

    def drain(self):
        """Đọc hết những gì đang có sẵn, không chờ. Trả (danh sách msg, eof)."""
        msgs, eof = [], False
        while select.select([self.sock], [], [], 0)[0]:
            try:
                chunk = self.sock.recv(65536)
            except ConnectionResetError:
                eof = True
                break
            if not chunk:
                eof = True
                break
            self.buf += chunk
        while (m := self._pop()) is not None:
            msgs.append(m)
        return msgs, eof

    def expect_closed(self, timeout=5):
        self.sock.settimeout(timeout)
        try:
            while True:
                if self.sock.recv(65536) == b"":
                    return True
        except ConnectionResetError:
            return True
        except socket.timeout:
            return False

    def close(self):
        self.sock.close()


class NetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.srv = Server(SERVER)

    @classmethod
    def tearDownClass(cls):
        cls.srv.stop()
        if "-v" in sys.argv:
            print(cls.srv.log)

    def client(self, **kw):
        c = Client(self.srv.port, **kw)
        self.addCleanup(c.close)
        return c

    # ---- cơ bản ----------------------------------------------------------
    def test_ping_pong_echoes_id(self):
        c = self.client()
        c.send_msg({"type": "PING", "id": "c-1"})
        self.assertEqual(c.recv_msg(), {"type": "PONG", "id": "c-1"})

    def test_wire_bytes_match_protocol_example(self):
        # Ví dụ mục 2: đúng các byte này trên dây. PING không có id vẫn hợp lệ
        # (mục 5.7) nên phải nhận lại PONG không có id.
        c = self.client()
        c.send(bytes.fromhex("0000000F7B2274797065223A2250494E47227D"))
        self.assertEqual(c.recv_msg(), {"type": "PONG"})

    # ---- framing ----------------------------------------------------------
    def test_header_split_byte_by_byte(self):
        c = self.client()
        data = frame({"type": "PING", "id": "c-2"})
        for i in range(len(data)):
            c.send(data[i:i + 1])
            time.sleep(0.005)
        self.assertEqual(c.recv_msg(), {"type": "PONG", "id": "c-2"})

    def test_many_frames_in_one_send(self):
        c = self.client()
        c.send(b"".join(frame({"type": "PING", "id": f"c-{i}"})
                        for i in range(10)))
        got = [c.recv_msg()["id"] for _ in range(10)]
        self.assertEqual(got, [f"c-{i}" for i in range(10)])

    def test_frame_split_across_sends(self):
        c = self.client()
        a, b = frame({"type": "PING", "id": "A"}), frame({"type": "PING", "id": "B"})
        data = a + b
        cut = len(a) + 2                          # giữa header của frame 2
        c.send(data[:cut])
        self.assertEqual(c.recv_msg()["id"], "A")
        time.sleep(0.05)
        c.send(data[cut:])
        self.assertEqual(c.recv_msg()["id"], "B")

    def test_max_size_payload_accepted(self):
        c = self.client()
        base = {"type": "PING", "id": "big", "data": {"pad": ""}}
        overhead = len(json.dumps(base, separators=(",", ":")))
        base["data"]["pad"] = "x" * (65536 - overhead)
        raw = json.dumps(base, separators=(",", ":")).encode()
        self.assertEqual(len(raw), 65536)
        c.send(frame(raw))
        self.assertEqual(c.recv_msg()["id"], "big")

    def test_zero_length_is_protocol_error_and_closes(self):
        c = self.client()
        c.send(struct.pack(">I", 0))
        r = c.recv_msg()
        self.assertEqual(r["error"]["code"], 10)
        self.assertTrue(c.expect_closed())

    def test_oversize_length_is_protocol_error_and_closes(self):
        c = self.client()
        c.send(struct.pack(">I", 65537))
        r = c.recv_msg()
        self.assertEqual(r["error"]["code"], 10)
        self.assertTrue(c.expect_closed())

    def test_garbage_stream_closes_without_crash(self):
        c = self.client()
        c.send(b"GET / HTTP/1.1\r\n\r\n")         # "GET " = length khổng lồ
        self.assertEqual(c.recv_msg()["error"]["code"], 10)
        self.assertTrue(c.expect_closed())
        d = self.client()                         # server vẫn sống
        d.send_msg({"type": "PING", "id": "ok"})
        self.assertEqual(d.recv_msg()["id"], "ok")

    # ---- message ----------------------------------------------------------
    def test_invalid_json_keeps_connection(self):
        c = self.client()
        c.send(frame(b"{not json"))
        self.assertEqual(c.recv_msg()["error"]["code"], 1)
        c.send_msg({"type": "PING", "id": "after"})
        self.assertEqual(c.recv_msg()["id"], "after")

    def test_unknown_type(self):
        c = self.client()
        c.send_msg({"type": "NOPE", "id": "c-7"})
        r = c.recv_msg()
        self.assertEqual((r["id"], r["ok"], r["error"]["code"]), ("c-7", False, 1))

    def test_missing_id(self):
        c = self.client()
        c.send_msg({"type": "LOGIN", "data": {}})
        r = c.recv_msg()
        self.assertEqual(r["error"]["code"], 1)
        self.assertNotIn("id", r)

    def test_auth_required_before_login(self):
        c = self.client()
        c.send_msg({"type": "FRIEND_LIST", "id": "c-3"})
        r = c.recv_msg()
        self.assertEqual((r["type"], r["id"], r["error"]["code"]),
                         ("FRIEND_LIST_RES", "c-3", 2))

    def test_command_without_handler_is_internal_error(self):
        c = self.client()
        c.send_msg({"type": "RESUME", "id": "c-4", "data": {}})
        r = c.recv_msg()
        self.assertEqual(r["type"], "RESUME_RES")
        self.assertEqual(r["error"]["code"], 11)

    def test_ping_with_data_and_without_id(self):
        c = self.client()
        c.send_msg({"type": "PING", "data": {"t": 1}})
        self.assertEqual(c.recv_msg(), {"type": "PONG"})

    def test_pong_from_client_is_ignored(self):
        c = self.client()
        c.send_msg({"type": "PONG"})
        c.send_msg({"type": "PING", "id": "x"})
        self.assertEqual(c.recv_msg()["id"], "x")  # không có phản hồi cho PONG

    def test_utf8_roundtrip_not_needed_but_unicode_id_ok(self):
        c = self.client()
        c.send_msg({"type": "PING", "id": "xin chào Bình 😀"})
        self.assertEqual(c.recv_msg()["id"], "xin chào Bình 😀")

    def test_rate_limit(self):
        c = self.client()
        c.send(b"".join(frame({"type": "PING", "id": str(i)})
                        for i in range(80)))
        codes = []
        for _ in range(80):
            r = c.recv_msg()
            codes.append(r.get("error", {}).get("code", 0))
        self.assertIn(9, codes)                   # có ERR_RATE_LIMIT
        self.assertGreaterEqual(codes.count(0), 20)

    # ---- kết nối ----------------------------------------------------------
    def test_abrupt_close_mid_frame_does_not_break_server(self):
        c = self.client()
        c.send(struct.pack(">I", 100) + b"{\"typ")  # frame dở rồi bỏ
        c.close()
        time.sleep(0.1)
        d = self.client()
        d.send_msg({"type": "PING", "id": "alive"})
        self.assertEqual(d.recv_msg()["id"], "alive")

    def test_many_concurrent_connections(self):
        clients = [self.client() for _ in range(300)]
        for i, c in enumerate(clients):
            c.send_msg({"type": "PING", "id": f"n{i}"})
        for i, c in enumerate(clients):
            self.assertEqual(c.recv_msg()["id"], f"n{i}")

    def test_connect_disconnect_churn(self):
        for _ in range(200):
            c = Client(self.srv.port)
            c.send_msg({"type": "PING", "id": "q"})
            c.recv_msg()
            c.close()
        d = self.client()
        d.send_msg({"type": "PING", "id": "end"})
        self.assertEqual(d.recv_msg()["id"], "end")

    def test_slow_reader_gets_disconnected(self):
        # Client không đọc, server cứ bị hỏi -> hàng đợi gửi vượt 1 MB -> đóng.
        # Để tránh bị kernel nuốt hết, client thu nhỏ bộ đệm nhận.
        c = self.client(rcvbuf=4096)
        c.sock.settimeout(0.5)
        payload = frame({"type": "PING", "id": "s" * 2000})
        closed = False
        try:
            for _ in range(60000):
                c.sock.sendall(payload)
        except (socket.timeout, BrokenPipeError, ConnectionResetError):
            closed = True
        # dù sendall có kẹt hay không, kết nối phải bị server cắt
        c.sock.settimeout(10)
        drained = 0
        try:
            while True:
                chunk = c.sock.recv(1 << 16)
                if not chunk:
                    closed = True
                    break
                drained += len(chunk)
        except ConnectionResetError:
            closed = True
        self.assertTrue(closed)
        # server vẫn phục vụ người khác
        d = self.client()
        d.send_msg({"type": "PING", "id": "fine"})
        self.assertEqual(d.recv_msg()["id"], "fine")

    def test_large_responses_fully_delivered_via_epollout(self):
        # Client thu nhỏ bộ đệm nhận và chưa đọc: server phải ghi dở, giữ phần
        # còn lại trong hàng đợi, rồi gửi nốt khi socket ghi được (EPOLLOUT).
        # 12 x ~60 KB = ~720 KB, dưới giới hạn hàng đợi 1 MB.
        c = self.client(rcvbuf=4096)
        n = 12
        for i in range(n):
            pad = chr(ord("a") + i) * 60000
            c.send_msg({"type": "PING", "id": f"{i}:{pad}"})
        time.sleep(1.0)                           # để hàng đợi phía server đầy
        for i in range(n):
            r = c.recv_msg()
            self.assertEqual(r["type"], "PONG")
            self.assertEqual(r["id"], f"{i}:" + chr(ord("a") + i) * 60000)


class AuthTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = Server(SERVER)

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def client(self):
        c = Client(self.server.port)
        self.addCleanup(c.close)
        return c

    def call(self, c, type_, data, id_="c-1"):
        c.send_msg({"type": type_, "id": id_, "data": data})
        return c.recv_msg()

    def register(self, c, user, pw="123456", name="Tên"):
        return self.call(c, "REGISTER", {"username": user, "password": pw,
                                         "display_name": name})

    def test_register_then_login(self):
        c = self.client()
        r = self.register(c, uname("an_test"), name="Ân")
        self.assertEqual((r["type"], r["id"], r["ok"]), ("REGISTER_RES", "c-1", True))
        r = self.call(c, "LOGIN", {"username": uname("an_test").upper(), "password": "123456"}, "c-2")
        self.assertTrue(r["ok"], r)
        self.assertEqual(r["data"]["username"], uname("an_test"))
        self.assertEqual(r["data"]["display_name"], "Ân")
        self.assertRegex(r["data"]["token"], r"^[0-9a-f]{64}$")
        evt = c.recv_msg()
        self.assertEqual(evt, {"type": "EVT_OFFLINE_MSGS", "data": {"messages": []}})
        # đã xác thực: lệnh cần đăng nhập không còn ERR_NOT_AUTHENTICATED
        c.send_msg({"type": "FRIEND_LIST", "id": "c-3"})
        self.assertNotEqual(c.recv_msg()["error"]["code"], 2)

    def test_register_duplicate(self):
        c = self.client()
        self.assertTrue(self.register(c, uname("dup_user"))["ok"])
        r = self.register(c, uname("dup_user").upper())
        self.assertEqual(r["error"]["code"], 6)

    def test_register_validation(self):
        c = self.client()
        bad = [
            dict(user="ab"), dict(user="a" * 21), dict(user="co dau"),
            dict(user="hợp_lệ"), dict(user="okuser", pw="12345"),
            dict(user="okuser", pw="x" * 65), dict(user="okuser", name=""),
            dict(user="okuser", name="x" * 31),
        ]
        for kw in bad:
            r = self.register(c, **kw)
            self.assertEqual(r["error"]["code"], 1, kw)
        r = self.call(c, "REGISTER", {"username": "okuser"})
        self.assertEqual(r["error"]["code"], 1)

    def test_login_errors(self):
        c = self.client()
        self.register(c, uname("err_user"))
        r = self.call(c, "LOGIN", {"username": uname("nobody"), "password": "123456"})
        self.assertEqual(r["error"]["code"], 1001)
        r = self.call(c, "LOGIN", {"username": uname("err_user"), "password": "wrong!!"})
        self.assertEqual(r["error"]["code"], 1002)
        r = self.call(c, "LOGIN", {"username": uname("err_user")})
        self.assertEqual(r["error"]["code"], 1)

    def test_login_elsewhere_kicks_old_connection(self):
        a, b = self.client(), self.client()
        self.register(a, uname("kick_user"))
        self.assertTrue(self.call(a, "LOGIN", {"username": uname("kick_user"),
                                               "password": "123456"})["ok"])
        a.recv_msg()  # EVT_OFFLINE_MSGS
        self.assertTrue(self.call(b, "LOGIN", {"username": uname("kick_user"),
                                               "password": "123456"})["ok"])
        r = a.recv_msg()
        self.assertEqual(r, {"type": "EVT_KICKED", "data": {"reason": "login_elsewhere"}})
        a.expect_closed()
        # kết nối mới vẫn dùng được sau khi kết nối cũ đóng
        b.recv_msg()
        b.send_msg({"type": "FRIEND_LIST", "id": "c-9"})
        self.assertNotEqual(b.recv_msg()["error"]["code"], 2)

    def test_second_login_on_same_connection_rejected(self):
        c = self.client()
        self.register(c, uname("twice_user"))
        self.call(c, "LOGIN", {"username": uname("twice_user"), "password": "123456"})
        c.recv_msg()
        r = self.call(c, "LOGIN", {"username": uname("twice_user"), "password": "123456"})
        self.assertEqual(r["error"]["code"], 1)


class TimeoutTest(unittest.TestCase):
    """Dùng bản server timeout ngắn: 2 giây chưa đăng nhập, 4 giây im lặng."""

    @classmethod
    def setUpClass(cls):
        if not SERVER_FAST:
            raise unittest.SkipTest("không có server timeout ngắn")
        cls.srv = Server(SERVER_FAST)

    @classmethod
    def tearDownClass(cls):
        if SERVER_FAST:
            cls.srv.stop()

    def test_unauthenticated_connection_is_kicked(self):
        c = Client(self.srv.port)
        self.addCleanup(c.close)
        t0 = time.monotonic()
        r = c.recv_msg()
        self.assertEqual((r["type"], r["data"]["reason"]), ("EVT_KICKED", "timeout"))
        self.assertTrue(c.expect_closed())
        self.assertLess(time.monotonic() - t0, 4)             # 2s + tối đa 1 tick
        self.assertGreaterEqual(time.monotonic() - t0, 1.9)   # không kick sớm

    def test_pinging_does_not_keep_unauthenticated_alive(self):
        c = Client(self.srv.port)
        self.addCleanup(c.close)
        kicked, t0 = False, time.monotonic()
        while time.monotonic() - t0 < 6:
            msgs, eof = c.drain()       # đọc hết trước khi gửi tiếp (tránh RST)
            kicked |= any(m.get("type") == "EVT_KICKED" for m in msgs)
            if eof:
                break
            try:
                c.send_msg({"type": "PING", "id": "k"})
            except OSError:
                break
            time.sleep(0.3)
        self.assertTrue(kicked)
        self.assertLess(time.monotonic() - t0, 5)   # bị đóng dù vẫn đang PING


if __name__ == "__main__":
    SERVER = sys.argv.pop(1)
    # đối số thứ hai (nếu là đường dẫn file chạy được) là server timeout ngắn
    SERVER_FAST = None
    if len(sys.argv) > 1 and "/" in sys.argv[1]:
        SERVER_FAST = sys.argv.pop(1)
    unittest.main(verbosity=2)
