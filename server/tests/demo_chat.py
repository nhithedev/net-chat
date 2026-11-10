"""Kịch bản demo: hai người dùng đăng ký, kết bạn, chat 1-1, rớt kết nối.

Dùng:  python3 -I tests/demo_chat.py [host] [port]      (mặc định 127.0.0.1 9000)
Giao thức v2: mỗi kết nối gửi HELLO (dạng rõ), rồi mọi payload được mã hóa Caesar;
khung trên TCP là [length][seq][ack][payload]. Mỗi dòng in ra có dạng
"An  C->S ..." hoặc "Binh S->C ... (seq=.. ack=..)" nên dùng thẳng để trình bày.
Server phải đang chạy.
Tên người dùng thêm hậu tố ngẫu nhiên để chạy lại nhiều lần không bị trùng.
"""
import json
import socket
import struct
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9000
SUFFIX = "%05x" % (int(time.time() * 1000) & 0xFFFFF)


HEADER = struct.Struct(">III")      # length, seq, ack
KEY = 3                             # khóa Caesar cố định (common/cipher.h)


class Client:
    """Một kết nối TCP thô: tự đóng khung 12 byte, đánh seq/ack, mã hóa Caesar sau HELLO."""

    def __init__(self, label):
        self.label = label
        self.sock = socket.create_connection((HOST, PORT), 5)
        self.sock.settimeout(3)
        self.buf = b""
        self.n = 0
        self.tx_seq = 0
        self.rx_seq = 0
        self.cipher = False
        self.hello()

    def _send_raw(self, raw):
        if self.cipher:
            raw = bytes((b + KEY) & 0xFF for b in raw)
        self.tx_seq += 1
        self.sock.sendall(HEADER.pack(len(raw), self.tx_seq, self.rx_seq) + raw)

    def hello(self):
        """HELLO một mình (rõ), chờ HELLO_RES (rõ), rồi bật mã hóa nếu server chọn."""
        req = {"type": "HELLO", "id": "k-1",
               "data": {"proto": 1, "client": "demo/0.1", "enc": ["caesar"], "ping_ms": 1000}}
        raw = json.dumps(req).encode()
        print("%-6s C->S %s  (seq=1, rõ)" % (self.label, raw.decode()))
        self._send_raw(raw)
        res = self._frame()
        print("%-6s S->C %s  (rõ)" % (self.label, json.dumps(res)))
        self.cipher = res["data"]["enc"] == "caesar"
        if self.cipher:
            print("       --- từ đây mọi payload đã mã hóa ---")

    def _frame(self):
        while True:
            if len(self.buf) >= HEADER.size:
                length, seq, ack = HEADER.unpack(self.buf[:HEADER.size])
                if len(self.buf) >= HEADER.size + length:
                    body = self.buf[HEADER.size:HEADER.size + length]
                    self.buf = self.buf[HEADER.size + length:]
                    self.rx_seq, self.last = seq, (seq, ack)
                    if self.cipher:
                        body = bytes((b - KEY) & 0xFF for b in body)
                    return json.loads(body)
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server đóng kết nối")
            self.buf += chunk

    def recv(self):
        msg = self._frame()
        print("%-6s S->C %s  (seq=%d ack=%d)" % (self.label, json.dumps(msg, ensure_ascii=False), *self.last))
        return msg

    def expect_event(self, evt):
        """Đọc tới khi gặp event `evt` (bỏ qua event khác như EVT_OFFLINE_MSGS)."""
        for _ in range(10):
            m = self.recv()
            if m.get("type") == evt:
                return m
        raise AssertionError("không thấy " + evt)

    def request(self, type_, data=None):
        self.n += 1
        rid = "c-%d" % self.n
        req = {"type": type_, "id": rid}
        if data is not None:
            req["data"] = data
        raw = json.dumps(req, ensure_ascii=False).encode()
        print("%-6s C->S %s  (seq=%d)" % (self.label, raw.decode(), self.tx_seq + 1))
        self._send_raw(raw)
        for _ in range(10):                 # bỏ qua event xen giữa, chờ đúng response
            m = self.recv()
            if m.get("id") == rid:
                return m
        raise AssertionError("không có response cho " + rid)

    def close(self):
        self.sock.close()


def check(cond, what):
    print("   %s %s" % ("OK  " if cond else "FAIL", what))
    if not cond:
        sys.exit(1)


def main():
    an_name, binh_name = "an_" + SUFFIX, "binh_" + SUFFIX
    an, binh = Client("An"), Client("Binh")

    print("\n--- 1. Đăng ký và đăng nhập ---")
    for c, name, disp in ((an, an_name, "An"), (binh, binh_name, "Bình")):
        r = c.request("REGISTER", {"username": name, "password": "123456", "display_name": disp})
        check(r["ok"], "REGISTER " + name)
    check(an.request("LOGIN", {"username": an_name, "password": "123456"})["ok"], "An đăng nhập")
    check(binh.request("LOGIN", {"username": binh_name, "password": "123456"})["ok"], "Bình đăng nhập")

    print("\n--- 2. Danh sách người dùng online ---")
    r = an.request("ONLINE_LIST")
    names = [u["username"] for u in r["data"]["users"]]
    check(an_name in names and binh_name in names, "cả hai đều có trong danh sách online")

    print("\n--- 3. Chưa là bạn thì không nhắn được ---")
    r = an.request("MSG_SEND", {"to": binh_name, "text": "chào"})
    check(not r["ok"] and r["error"]["code"] == 4, "ERR_NOT_FRIENDS (4)")

    print("\n--- 4. Kết bạn ---")
    check(an.request("FRIEND_REQUEST", {"to": binh_name})["ok"], "An gửi lời mời")
    evt = binh.expect_event("EVT_FRIEND_REQUEST")
    check(evt["data"]["from"] == an_name, "Bình nhận EVT_FRIEND_REQUEST")
    r = an.request("FRIEND_REQUEST", {"to": binh_name})
    check(not r["ok"] and r["error"]["code"] == 6, "mời lại bị ERR_ALREADY_EXISTS (6)")
    check(binh.request("FRIEND_ACCEPT", {"from": an_name})["ok"], "Bình chấp nhận")
    an.expect_event("EVT_FRIEND_ACCEPTED")
    r = an.request("FRIEND_LIST")
    check(r["data"]["friends"][0]["username"] == binh_name and r["data"]["friends"][0]["online"],
          "FRIEND_LIST của An: Bình là bạn và đang online")

    print("\n--- 5. Chat 1-1 ---")
    check(an.request("CHAT_OPEN", {"with": binh_name})["ok"], "An mở chat với Bình")
    check(binh.request("CHAT_OPEN", {"with": an_name})["ok"], "Bình mở chat với An")
    r = an.request("MSG_SEND", {"to": binh_name, "text": "chào Bình, tiếng Việt có dấu ✓"})
    check(r["ok"] and r["data"]["delivered"], "MSG_SEND delivered=true")
    evt = binh.expect_event("EVT_MSG")
    check(evt["data"]["text"] == "chào Bình, tiếng Việt có dấu ✓", "Bình nhận EVT_MSG đúng nội dung")
    check(binh.request("MSG_SEND", {"to": an_name, "text": "chào An"})["ok"], "Bình trả lời")
    an.expect_event("EVT_MSG")
    r = an.request("CHAT_OPEN", {"with": binh_name})
    check([m["text"] for m in r["data"]["history"]][-2:] == ["chào Bình, tiếng Việt có dấu ✓", "chào An"],
          "CHAT_OPEN trả lịch sử theo thứ tự thời gian")

    print("\n--- 6. Dừng cuộc trò chuyện ---")
    check(an.request("CHAT_CLOSE", {"with": binh_name})["ok"], "An dừng chat")
    evt = binh.expect_event("EVT_CHAT_CLOSED")
    check(evt["data"]["username"] == an_name, "Bình nhận EVT_CHAT_CLOSED")

    print("\n--- 7. Một bên rớt mạng khi đang chat ---")
    check(an.request("CHAT_OPEN", {"with": binh_name})["ok"], "An mở lại chat")
    check(binh.request("CHAT_OPEN", {"with": an_name})["ok"], "Bình mở chat")
    an.close()
    got = {binh.recv()["type"], binh.recv()["type"]}
    check(got == {"EVT_PEER_DISCONNECTED", "EVT_USER_OFFLINE"}, "Bình nhận PEER_DISCONNECTED và USER_OFFLINE")

    print("\n--- 8. Nhắn cho bạn đang offline ---")
    r = binh.request("MSG_SEND", {"to": an_name, "text": "ngủ chưa?"})
    check(r["ok"] and not r["data"]["delivered"], "MSG_SEND delivered=false (lưu offline)")

    binh.close()
    print("\nTẤT CẢ ĐỀU ĐẠT")


if __name__ == "__main__":
    main()
