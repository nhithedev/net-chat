import { useEffect, useRef, useState } from 'react'
import { ErrorCode, type KickReason } from '../types/protocol'
import type { ConnectionStatus } from '../types/domain'

type Callbacks = {
  onSessionExpired?: () => void
  onDisconnected?: (reason: KickReason) => void
}

// Mô phỏng trạng thái WebSocket cho prototype (F-UI-6).
export default function useConnectionStatus({ onSessionExpired, onDisconnected }: Callbacks = {}) {
  const [status, setStatus] = useState<ConnectionStatus>('connecting')
  const timers = useRef<number[]>([])

  useEffect(() => {
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
  }, [])

  function resumeFailed(code: number) {
    if (code === ErrorCode.TokenInvalid) onSessionExpired?.()
  }

  function connectionFailed(reason: KickReason) {
    onDisconnected?.(reason)
  }

  return { status, resumeFailed, connectionFailed }
}
