import type { ReactNode } from 'react'
import { cx } from '../../lib/cx'
import './Card.css'

type CardProps = {
  title?: string
  aside?: ReactNode
  className?: string
  children: ReactNode
}

export default function Card({ title, aside, className, children }: CardProps) {
  return (
    <section className={cx('card card--col', className)}>
      {title && (
        <div className="row card__head">
          <h2 className="grow">{title}</h2>
          {aside}
        </div>
      )}
      {children}
    </section>
  )
}
