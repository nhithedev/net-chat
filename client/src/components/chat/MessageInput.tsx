import { useState, type FormEvent } from 'react'
import Button from '../common/Button'

type MessageInputProps = {
  disabled?: boolean
  placeholder?: string
  onSend: (text: string) => void
}

export default function MessageInput({ disabled, placeholder = 'Viết tin nhắn...', onSend }: MessageInputProps) {
  const [text, setText] = useState('')

  function submit(event: FormEvent) {
    event.preventDefault()
    const clean = text.trim()
    if (!clean || disabled) return
    onSend(clean)
    setText('')
  }

  return (
    <form className="message-input row" onSubmit={submit}>
      <input
        className="grow"
        value={text}
        disabled={disabled}
        placeholder={placeholder}
        aria-label="Nội dung tin nhắn"
        onChange={(event) => setText(event.target.value)}
      />
      <Button type="submit" shape="icon" icon="send" ariaLabel="Gửi tin nhắn" disabled={disabled} />
    </form>
  )
}
