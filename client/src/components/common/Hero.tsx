import type { ReactNode } from 'react'

type HeroProps = {
  avatar?: ReactNode
  title: string
  subtitle?: string
  children?: ReactNode
}

export default function Hero({ avatar, title, subtitle, children }: HeroProps) {
  return (
    <div className="hero">
      {avatar}
      <h2>{title}</h2>
      {subtitle && <small>{subtitle}</small>}
      {children}
    </div>
  )
}
