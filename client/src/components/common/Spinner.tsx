import type { CSSProperties } from 'react'

type SpinnerProps = {
  size?: number
}

export default function Spinner({ size }: SpinnerProps) {
  const style = size ? ({ '--size': `${size}px` } as CSSProperties) : undefined
  return <i className="spinner" style={style} role="progressbar" aria-label="Đang tải" />
}
