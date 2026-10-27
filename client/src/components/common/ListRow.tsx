import type { ReactNode } from 'react'
import { cx } from '../../lib/cx'
import './ListRow.css'

type ListRowProps = {
  as?: 'div' | 'li' | 'label'
  avatar?: ReactNode
  title: ReactNode
  subtitle?: ReactNode
  leading?: ReactNode
  trailing?: ReactNode
  selected?: boolean
  className?: string
  onClick?: () => void
}

export default function ListRow({
  as: Root = 'li',
  avatar,
  title,
  subtitle,
  leading,
  trailing,
  selected,
  className,
  onClick,
}: ListRowProps) {
  const body = (
    <>
      {avatar}
      <span className="list-row__text">
        <span className="list-row__title truncate">{title}</span>
        {subtitle && <small className="truncate">{subtitle}</small>}
      </span>
    </>
  )

  return (
    <Root className={cx('list-row interactive', className)} aria-current={selected || undefined}>
      {leading}
      {onClick ? (
        <button className="list-row__main" type="button" onClick={onClick}>
          {body}
        </button>
      ) : (
        <div className="list-row__main">{body}</div>
      )}
      {trailing}
    </Root>
  )
}
