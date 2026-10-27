import { useState } from 'react'
import type {
  ChangeEventHandler,
  InputHTMLAttributes,
  TextareaHTMLAttributes,
} from 'react'
import Button from './Button'
import './TextField.css'

type TextFieldProps = Omit<InputHTMLAttributes<HTMLInputElement>, 'onChange'> & {
  label?: string
  error?: string
  multiline?: boolean
  onChange?: ChangeEventHandler<HTMLInputElement | HTMLTextAreaElement>
}

// Ô nhập `type="password"` tự có nút mắt để hiện/ẩn mật khẩu.
export default function TextField({ label, error, multiline = false, ...props }: TextFieldProps) {
  const [revealed, setRevealed] = useState(false)
  const controlProps = { className: 'field__control', 'aria-invalid': error ? true : undefined }
  const isPassword = !multiline && props.type === 'password'

  return (
    <label className="field">
      {label && <span>{label}</span>}
      {multiline ? (
        <textarea {...controlProps} {...(props as TextareaHTMLAttributes<HTMLTextAreaElement>)} />
      ) : (
        <div className="field__wrap">
          <input {...controlProps} {...props} type={isPassword && revealed ? 'text' : props.type} data-toggle={isPassword || undefined} />
          {isPassword && (
            <span className="field__toggle">
              <Button
                shape="icon"
                size="sm"
                variant="ghost"
                icon={revealed ? 'eyeOff' : 'eye'}
                ariaLabel={revealed ? 'Ẩn mật khẩu' : 'Hiện mật khẩu'}
                aria-pressed={revealed}
                onClick={() => setRevealed((current) => !current)}
              />
            </span>
          )}
        </div>
      )}
      {error && <small className="error">{error}</small>}
    </label>
  )
}
