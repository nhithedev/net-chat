import type { ReactNode } from 'react'

export default function SystemMessage({ children }: { children: ReactNode }) {
  return (
    <div className="system-message">
      <span>{children}</span>
    </div>
  )
}
