import type { ReactNode } from 'react'
import './Banner.css'

type BannerProps = {
  tone?: 'info' | 'warning' | 'danger'
  inline?: boolean // hộp bo góc trong form thay vì dải toàn chiều rộng
  children: ReactNode
}

export default function Banner({ tone = 'info', inline = false, children }: BannerProps) {
  return (
    <div className="banner" data-tone={tone} data-inline={inline || undefined} role="status">
      {children}
    </div>
  )
}
