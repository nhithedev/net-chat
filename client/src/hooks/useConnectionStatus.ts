import { useEffect, useRef, useState } from 'react'
import { hasServer } from '../lib/netchat'
import { ErrorCode, type KickReason } from '../types/protocol'
import type { ConnectionStatus } from '../types/domain'

type Callbacks = {
  onSessionExpired?: () => void
  onDisconnected?: (reason: KickReason) => void
  onKicked?: (reason: KickReason) => void
}

// F-UI-6: trạng thái kết nối. Trong Electron lấy từ lõi mạng (lõi tự kết nối lại và
// RESUME khi rớt mạng, protocol-v2 4.6); ngoài Electron mô phỏng cho prototype.
export default function useConnectionStatus({ onSessionExpired, onDisconnected, onKicked }: Callbacks = {}) {
  const live = hasServer()
  // ChatPage chỉ hiện sau khi đăng nhập, tức kết nối đang mở.
  const [status, setStatus] = useState<ConnectionStatus>(live ? 'open' : 'connecting')
  const timers = useRef<number[]>([])
  // Callback đổi theo mỗi lần render; giữ bản mới nhất để không phải đăng ký lại sự kiện.
  const callbacks = useRef({ onSessionExpired, onDisconnected, onKicked })
  useEffect(() => {
    callbacks.current = { onSessionExpired, onDisconnected, onKicked }
  })

  useEffect(() => {
    const api = window.netchat
    if (!api) return
    const offStatus = api.onStatus(({ state, reason }) => {
      setStatus(state)
      // 'closed' khi đang đăng nhập = không khôi phục được phiên (token sai/hết hạn,
      // quá 60 giây không kết nối lại được): về màn đăng nhập. Đóng chủ động bỏ qua.
      if (state === 'closed' && reason !== 'client_closed') {
        if (reason === 'token_invalid') callbacks.current.onSessionExpired?.()
        else callbacks.current.onDisconnected?.('timeout')
      }
    })
    const offEvent = api.onEvent((evt) => {
      if (evt.type !== 'EVT_KICKED') return
      const reason = (evt.data as { reason?: KickReason } | undefined)?.reason
      if (reason === 'login_elsewhere') callbacks.current.onKicked?.(reason)
    })
    return () => {
      offStatus()
      offEvent()
    }
  }, [])

  useEffect(() => {
    if (live) return
    function clearTimers() {
      timers.current.forEach((timer) => window.clearTimeout(timer))
      timers.current = []
    }

    function reconnect() {
      clearTimers()
      setStatus('reconnecting')
      timers.current.push(window.setTimeout(() => setStatus('restored'), 700))
      timers.current.push(window.setTimeout(() => setStatus('open'), 2200))
    }

    function disconnect() {
      clearTimers()
      setStatus('closed')
    }

    timers.current.push(window.setTimeout(() => setStatus('open'), 700))
    window.addEventListener('offline', disconnect)
    window.addEventListener('online', reconnect)

    return () => {
      clearTimers()
      window.removeEventListener('offline', disconnect)
      window.removeEventListener('online', reconnect)
    }
  }, [live])

  function resumeFailed(code: number) {
    if (code === ErrorCode.TokenInvalid) onSessionExpired?.()
  }

  function connectionFailed(reason: KickReason) {
    onDisconnected?.(reason)
  }

  return { status, resumeFailed, connectionFailed }
}
