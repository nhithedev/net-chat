// Cầu nối giữa Electron main và lõi mạng C (protocol-v2.md mục 10.3, 10.4).
//
// - Khởi chạy `netchat-core` làm tiến trình con, nói chuyện qua stdin/stdout.
// - Pipe là luồng byte nên dùng khung  [uint32 BE length][uint8 kind][body]
//   (length = 1 + độ dài body), tự ghép/tách như với TCP.
// - Giữ bảng  id -> Promise  để trả response về đúng chỗ gọi; message không có
//   id khớp (EVT_*) được chuyển lên giao diện qua onEvent.
// File này KHÔNG mở socket: mọi việc mạng do lõi C làm.
const { spawn } = require('child_process')

const K_SEND = 0x01
const K_CTRL = 0x02
const K_RECV = 0x81
const K_STATUS = 0x82

const CONNECT_TIMEOUT_MS = 8000
const REQUEST_TIMEOUT_MS = 10000

const NOT_CONNECTED = { ok: false, error: { code: -1, message: 'Chưa kết nối tới máy chủ' } }
const CONNECTION_LOST = { ok: false, error: { code: -2, message: 'Mất kết nối tới máy chủ' } }
const TIMEOUT = { ok: false, error: { code: -3, message: 'Máy chủ không phản hồi' } }

function createCoreLink({ corePath, onEvent, onStatus, onLog }) {
  let core = null
  let rx = Buffer.alloc(0)
  let state = 'closed' // connecting | open | closed
  let nextId = 1
  const pending = new Map() // id -> { resolve, timer }
  let waiters = [] // đang chờ kết quả của connect()

  function setState(next, reason) {
    state = next
    onStatus?.({ state: next, reason })
    if (next === 'open' || next === 'closed') {
      const w = waiters
      waiters = []
      w.forEach((fn) => fn(next, reason))
    }
    if (next === 'closed') failAllPending()
  }

  function failAllPending() {
    for (const [id, p] of pending) {
      clearTimeout(p.timer)
      p.resolve(CONNECTION_LOST)
      pending.delete(id)
    }
  }

  function writeFrame(kind, bodyObj) {
    const body = Buffer.from(JSON.stringify(bodyObj), 'utf8')
    const head = Buffer.alloc(5)
    head.writeUInt32BE(1 + body.length, 0)
    head.writeUInt8(kind, 4)
    core.stdin.write(Buffer.concat([head, body]))
  }

  function handleFrame(kind, text) {
    let msg
    try {
      msg = JSON.parse(text)
    } catch {
      onLog?.('core gửi JSON hỏng: ' + text.slice(0, 80))
      return
    }
    if (kind === K_STATUS) {
      setState(msg.state, msg.reason)
    } else if (kind === K_RECV) {
      const p = msg.id !== undefined ? pending.get(msg.id) : undefined
      if (p) {
        clearTimeout(p.timer)
        pending.delete(msg.id)
        p.resolve(msg.ok ? { ok: true, data: msg.data ?? {} } : { ok: false, error: msg.error })
      } else if (typeof msg.type === 'string' && msg.type.startsWith('EVT_')) {
        onEvent?.(msg)
      }
      // PONG và response muộn (đã quá hạn) bị bỏ qua.
    }
  }

  // Ghép khung từ stdout của lõi: một lần 'data' có thể chứa nhiều khung hoặc nửa khung.
  function onCoreData(chunk) {
    rx = Buffer.concat([rx, chunk])
    while (rx.length >= 4) {
      const len = rx.readUInt32BE(0)
      if (rx.length < 4 + len) return
      const kind = rx.readUInt8(4)
      const text = rx.subarray(5, 4 + len).toString('utf8')
      rx = rx.subarray(4 + len)
      handleFrame(kind, text)
    }
  }

  function start() {
    if (core) return
    core = spawn(corePath, [], { stdio: ['pipe', 'pipe', 'pipe'] })
    core.stdout.on('data', onCoreData)
    core.stderr.on('data', (d) => onLog?.('core: ' + d.toString().trim()))
    core.on('error', (err) => {
      onLog?.('không chạy được lõi mạng: ' + err.message)
      core = null
      setState('closed', 'core_missing')
    })
    core.on('exit', () => {
      core = null
      rx = Buffer.alloc(0)
      setState('closed', 'core_exit')
    })
  }

  // Kết nối (hoặc dùng lại kết nối đang mở). Trả { ok } hoặc { ok:false, error }.
  function connect({ host = '127.0.0.1', port = 9000 } = {}) {
    start()
    if (!core) return Promise.resolve({ ok: false, error: { code: -4, message: 'Không tìm thấy lõi mạng' } })
    if (state === 'open') return Promise.resolve({ ok: true })

    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        waiters = waiters.filter((w) => w !== done)
        resolve({ ok: false, error: { code: -3, message: 'Kết nối máy chủ quá hạn' } })
      }, CONNECT_TIMEOUT_MS)
      const done = (s, reason) => {
        clearTimeout(timer)
        resolve(s === 'open' ? { ok: true } : { ok: false, error: { code: -1, message: 'Không kết nối được máy chủ (' + reason + ')' } })
      }
      waiters.push(done)
      if (state !== 'connecting') writeFrame(K_CTRL, { cmd: 'connect', host, port })
    })
  }

  function disconnect() {
    if (core && state !== 'closed') writeFrame(K_CTRL, { cmd: 'close' })
    return Promise.resolve()
  }

  function request(type, data) {
    if (!core || state !== 'open') return Promise.resolve(NOT_CONNECTED)
    const id = 'c-' + nextId++
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        pending.delete(id)
        resolve(TIMEOUT)
      }, REQUEST_TIMEOUT_MS)
      pending.set(id, { resolve, timer })
      writeFrame(K_SEND, data === undefined ? { type, id } : { type, id, data })
    })
  }

  function stop() {
    if (core) {
      core.stdin.end() // lõi tự thoát khi stdin đóng
      core = null
    }
  }

  return { connect, disconnect, request, stop, getState: () => state }
}

module.exports = { createCoreLink }
