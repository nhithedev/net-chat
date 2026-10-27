import type { ReactNode } from 'react'
import Button from './Button'
import Overlay from './Overlay'
import './Dialog.css'

type DialogProps = {
  open: boolean
  title: string
  size?: 'sm' | 'md' | 'lg'
  actions?: ReactNode
  onClose?: () => void
  children: ReactNode
}

export default function Dialog({ open, title, size, actions, onClose, children }: DialogProps) {
  if (!open) return null
  return (
    <Overlay label={title} size={size} onClose={onClose}>
      <header className="row dialog__header">
        <h2 className="grow">{title}</h2>
        <Button shape="icon" size="sm" variant="outline" icon="close" ariaLabel="Đóng" onClick={onClose} />
      </header>
      <div className="dialog__body">{children}</div>
      {actions && <footer className="dialog__footer">{actions}</footer>}
    </Overlay>
  )
}
