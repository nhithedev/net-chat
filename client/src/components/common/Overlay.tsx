import { useEffect, useRef, type MouseEvent, type ReactNode } from 'react'
import './Overlay.css'

type OverlayProps = {
  label: string
  size?: 'sm' | 'md' | 'lg'
  onClose?: () => void
  children: ReactNode
}

// Các overlay đang mở, cái mở sau cùng nằm cuối: Esc chỉ đóng cái trên cùng.
const openOverlays: object[] = []

// Nền mờ + panel căn giữa. Đóng khi bấm ra ngoài hoặc nhấn Esc.
export default function Overlay({ label, size = 'md', onClose, children }: OverlayProps) {
  const closeRef = useRef(onClose)
  useEffect(() => {
    closeRef.current = onClose
  })

  useEffect(() => {
    const token = {}
    openOverlays.push(token)
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape' && openOverlays[openOverlays.length - 1] === token) closeRef.current?.()
    }
    window.addEventListener('keydown', onKeyDown)
    return () => {
      openOverlays.splice(openOverlays.indexOf(token), 1)
      window.removeEventListener('keydown', onKeyDown)
    }
  }, [])

  function closeOnBackdrop(event: MouseEvent<HTMLDivElement>) {
    if (event.target === event.currentTarget) onClose?.()
  }

  return (
    <div className="overlay" role="presentation" onMouseDown={closeOnBackdrop}>
      <section className="panel" data-size={size} role="dialog" aria-modal="true" aria-label={label}>
        {children}
      </section>
    </div>
  )
}
