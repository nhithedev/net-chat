import { useRef, useState } from 'react'
import type { Toast } from '../types/domain'

export default function useToasts(initial: Toast[] = []) {
  const [toasts, setToasts] = useState<Toast[]>(initial)
  const counter = useRef(0)

  function addToast(text: string, kind: Toast['kind'] = 'info') {
    counter.current += 1
    const id = `toast-${Date.now()}-${counter.current}`
    setToasts((current) => [...current, { id, kind, text }])
  }

  function dismissToast(id: string) {
    setToasts((current) => current.filter((toast) => toast.id !== id))
  }

  return { toasts, addToast, dismissToast }
}
