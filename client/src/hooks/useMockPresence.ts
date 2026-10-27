import { useEffect, useRef } from 'react'
import type { Friend } from '../types/domain'

const MIN_MS = 6000
const MAX_MS = 14000

// Mô phỏng người dùng khác kết nối/ngắt kết nối (thay cho EVT_USER_ONLINE/OFFLINE từ server).
export default function useMockPresence(users: Friend[], onChange: (username: string, online: boolean) => void) {
  const latest = useRef({ users, onChange })

  useEffect(() => {
    latest.current = { users, onChange }
  })

  useEffect(() => {
    let timer = 0
    const tick = () => {
      const { users: pool, onChange: notify } = latest.current
      if (pool.length > 0) {
        const user = pool[Math.floor(Math.random() * pool.length)]
        notify(user.username, !user.online)
      }
      timer = window.setTimeout(tick, MIN_MS + Math.random() * (MAX_MS - MIN_MS))
    }
    timer = window.setTimeout(tick, MIN_MS)
    return () => window.clearTimeout(timer)
  }, [])
}
