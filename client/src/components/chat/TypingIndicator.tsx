import './TypingIndicator.css'

// P2: chỉ báo "đang gõ".
export default function TypingIndicator({ name }: { name: string }) {
  return (
    <div className="typing-indicator subtle">
      <i className="dot" />
      <i className="dot" />
      <i className="dot" />
      <span>{name} đang gõ...</span>
    </div>
  )
}
