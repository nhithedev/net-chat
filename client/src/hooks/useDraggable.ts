import { useRef, useState, type CSSProperties, type PointerEvent } from 'react'

// Kéo một phần tử position: fixed bằng tay cầm (header). Không kéo khi bấm vào nút bên trong.
export default function useDraggable<T extends HTMLElement>() {
  const ref = useRef<T>(null)
  const [position, setPosition] = useState<{ left: number; top: number } | null>(null)
  const grab = useRef<{ dx: number; dy: number } | null>(null)

  function onPointerDown(event: PointerEvent<HTMLElement>) {
    if (!ref.current || (event.target as HTMLElement).closest('button')) return
    const rect = ref.current.getBoundingClientRect()
    grab.current = { dx: event.clientX - rect.left, dy: event.clientY - rect.top }
    event.currentTarget.setPointerCapture(event.pointerId)
  }

  function onPointerMove(event: PointerEvent<HTMLElement>) {
    if (!grab.current || !ref.current) return
    const { width, height } = ref.current.getBoundingClientRect()
    const clamp = (value: number, max: number) => Math.min(Math.max(0, value), Math.max(0, max))
    setPosition({
      left: clamp(event.clientX - grab.current.dx, window.innerWidth - width),
      top: clamp(event.clientY - grab.current.dy, window.innerHeight - height),
    })
  }

  function release() {
    grab.current = null
  }

  // Chưa kéo lần nào thì để CSS đặt vị trí mặc định.
  const style: CSSProperties | undefined = position ? { ...position, right: 'auto', bottom: 'auto' } : undefined

  return { ref, style, handleProps: { onPointerDown, onPointerMove, onPointerUp: release, onPointerCancel: release } }
}
