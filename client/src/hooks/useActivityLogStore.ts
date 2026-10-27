import { useCallback, useEffect, useRef, useState } from 'react'
import { seedActivityLogs } from '../mocks/activity'
import type { ActivityEvent, ActivityLogEntry, LogDetail } from '../types/domain'

const MAX_ENTRIES = 200

type LogOptions = { username?: string; detail?: LogDetail }

function createInitial() {
  const seed = seedActivityLogs()
  const connections = new Map<string, number>() // username -> conn_id đang mở
  let maxConn = 0
  for (const entry of seed) {
    if (entry.connId) maxConn = Math.max(maxConn, entry.connId)
    if (entry.username && entry.connId) {
      if (entry.event === 'login_ok') connections.set(entry.username, entry.connId)
      if (entry.event === 'disconnect') connections.delete(entry.username)
    }
  }
  // Kết nối của chính trình duyệt này.
  const own = maxConn + 1
  const entries: ActivityLogEntry[] = [...seed, { id: seed.length + 1, ts: Date.now(), connId: own, event: 'connect' }]
  return { entries, connections, own, nextId: entries.length + 1, nextConn: own + 1 }
}

// Nguồn log hoạt động dùng chung cho mọi trang. Tạm là mock cục bộ; sau này đọc từ activity_logs.
export default function useActivityLogStore(actor?: string) {
  const [initial] = useState(createInitial)
  const [entries, setEntries] = useState(initial.entries)
  const [panelOpen, setPanelOpen] = useState(false)
  const ids = useRef({ nextId: initial.nextId, nextConn: initial.nextConn, own: initial.own, connections: initial.connections })
  const actorRef = useRef(actor)

  useEffect(() => {
    actorRef.current = actor
  }, [actor])

  const append = useCallback((entry: Omit<ActivityLogEntry, 'id' | 'ts'>) => {
    const next = { ...entry, id: ids.current.nextId++, ts: Date.now() }
    setEntries((current) => [...current, next].slice(-MAX_ENTRIES))
  }, [])

  // Sự kiện của chính người dùng này (kết nối hiện tại của trình duyệt).
  const log = useCallback(
    (event: ActivityEvent, { username, detail }: LogOptions = {}) => {
      append({ event, detail, connId: ids.current.own, username: username ?? actorRef.current })
    },
    [append],
  )

  // Mở kết nối mới. Có `username` là người khác vừa kết nối + đăng nhập; không có là kết nối của chính mình.
  const connect = useCallback(
    (username?: string) => {
      const connId = ids.current.nextConn++
      append({ event: 'connect', connId })
      if (username) {
        ids.current.connections.set(username, connId)
        append({ event: 'login_ok', connId, username })
      } else {
        ids.current.own = connId
      }
    },
    [append],
  )

  const disconnect = useCallback(
    (username: string) => {
      append({ event: 'disconnect', connId: ids.current.connections.get(username), username })
      ids.current.connections.delete(username)
    },
    [append],
  )

  return { entries, panelOpen, setPanelOpen, log, connect, disconnect }
}

export type ActivityLogApi = ReturnType<typeof useActivityLogStore>
