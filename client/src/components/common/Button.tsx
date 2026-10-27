import type { ButtonHTMLAttributes } from 'react'
import Icon, { type IconName } from './Icon'
import './Button.css'

type ButtonProps = ButtonHTMLAttributes<HTMLButtonElement> & {
  variant?: 'primary' | 'soft' | 'ghost' | 'outline' | 'danger'
  shape?: 'default' | 'pill' | 'icon'
  size?: 'md' | 'sm'
  icon?: IconName
  ariaLabel?: string
}

export default function Button({
  variant = 'primary',
  shape = 'default',
  size = 'md',
  icon,
  ariaLabel,
  children,
  ...props
}: ButtonProps) {
  return (
    <button
      className="btn"
      type="button"
      data-variant={variant}
      data-shape={shape}
      data-size={size}
      aria-label={ariaLabel}
      title={shape === 'icon' ? ariaLabel : undefined}
      {...props}
    >
      {icon && <Icon name={icon} size={size === 'sm' ? 16 : 18} />}
      {children}
    </button>
  )
}
