import type {
  ChangeEventHandler,
  InputHTMLAttributes,
  TextareaHTMLAttributes,
} from 'react'
import './TextField.css'

type TextFieldProps = Omit<InputHTMLAttributes<HTMLInputElement>, 'onChange'> & {
  label?: string
  error?: string
  multiline?: boolean
  onChange?: ChangeEventHandler<HTMLInputElement | HTMLTextAreaElement>
}

export default function TextField({ label, error, multiline = false, ...props }: TextFieldProps) {
  const controlProps = { className: 'field__control', 'aria-invalid': error ? true : undefined }
  return (
    <label className="field">
      {label && <span>{label}</span>}
      {multiline ? (
        <textarea {...controlProps} {...(props as TextareaHTMLAttributes<HTMLTextAreaElement>)} />
      ) : (
        <input {...controlProps} {...props} />
      )}
      {error && <small className="error">{error}</small>}
    </label>
  )
}
