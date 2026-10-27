import type { ReactNode } from 'react'
import inboxEmpty from '../../../assets/inbox_empty.png'
import './EmptyState.css'

type EmptyStateProps = {
  title?: string
  text?: string
  action?: ReactNode
  icon?: ReactNode // mặc định là ảnh hộp thư trống; truyền null để ẩn
  compact?: boolean
}

export default function EmptyState({ title, text, action, icon, compact = false }: EmptyStateProps) {
  return (
    <div className="empty-state" data-compact={compact || undefined}>
      {icon === undefined ? <img className="empty-state__image" src={inboxEmpty} alt="" /> : icon}
      {title && <h2>{title}</h2>}
      {text && <p className="subtle">{text}</p>}
      {action}
    </div>
  )
}
