import './Badge.css'

type BadgeProps = {
  count?: number
  label?: string
  variant?: 'solid' | 'muted' | 'accent'
}

export default function Badge({ count, label, variant = 'solid' }: BadgeProps) {
  const text = label ?? (count !== undefined && count > 99 ? '99+' : count)
  return (
    <span className="badge" data-variant={variant}>
      {text}
    </span>
  )
}
