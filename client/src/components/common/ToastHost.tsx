import { useEffect, useRef } from 'react'
import type { Toast } from '../../types/domain'
import Button from './Button'
import './ToastHost.css'

type ToastHostProps = {
  toasts: Toast[]
  onDismiss: (id: string) => void
}

const TOAST_MAX_MS = 10_000

type ToastItemProps = {
  toast: Toast
  onDismiss: (id: string) => void
}

// Mỗi toast tự đóng sau tối đa 10 giây (kể cả toast có sẵn lúc mở trang).
function ToastItem({ toast, onDismiss }: ToastItemProps) {
  // onDismiss đổi identity mỗi lần render cha; giữ qua ref để timer không bị reset.
  const dismissRef = useRef(onDismiss)
  useEffect(() => {
    dismissRef.current = onDismiss
  })

  useEffect(() => {
    const timer = window.setTimeout(() => dismissRef.current(toast.id), TOAST_MAX_MS)
    return () => window.clearTimeout(timer)
  }, [toast.id])

  return (
    <div className="toast row" data-tone={toast.kind === 'error' ? 'danger' : 'info'}>
      <span className="grow">{toast.text}</span>
      <Button shape="icon" size="sm" variant="outline" icon="close" ariaLabel="Đóng thông báo" onClick={() => onDismiss(toast.id)} />
    </div>
  )
}

export default function ToastHost({ toasts, onDismiss }: ToastHostProps) {
  return (
    <div className="toast-host" aria-live="polite">
      {toasts.map((toast) => (
        <ToastItem key={toast.id} toast={toast} onDismiss={onDismiss} />
      ))}
    </div>
  )
}
