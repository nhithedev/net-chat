import { formatFullTime, formatTime } from '../../lib/format'
import type { ChatMessage } from '../../types/domain'
import EmptyState from '../common/EmptyState'
import Spinner from '../common/Spinner'
import MessageBubble from './MessageBubble'
import SystemMessage from './SystemMessage'

type MessageListProps = {
  messages: ChatMessage[]
  currentUser: string
  loading?: boolean
}

const BURST_MS = 60_000

// Hai tin liền nhau: cùng người gửi và cách nhau không quá 1 phút.
function isSameBurst(a: ChatMessage | undefined, b: ChatMessage | undefined): boolean {
  if (!a || !b || a.system || b.system) return false
  return a.from === b.from && Math.abs(b.ts - a.ts) <= BURST_MS
}

export default function MessageList({ messages, currentUser, loading }: MessageListProps) {
  if (loading) {
    return (
      <div className="message-list">
        <EmptyState compact icon={<Spinner />} text="Đang tải lịch sử..." />
      </div>
    )
  }

  if (messages.length === 0) {
    return (
      <div className="message-list">
        <EmptyState compact icon={null} title="Chưa có tin nhắn" text="Gửi lời chào để bắt đầu cuộc trò chuyện." />
      </div>
    )
  }

  return (
    <div className="message-list">
      {messages.map((message, index) => {
        if (message.system) return <SystemMessage key={message.msgId}>{message.text}</SystemMessage>
        const mine = message.from === currentUser
        // Tin liền nhau của cùng người trong 1 phút: chỉ tin đầu hiện giờ/tên/avatar, chỉ tin cuối hiện trạng thái.
        const continued = isSameBurst(messages[index - 1], message)
        const lastOfBurst = !isSameBurst(message, messages[index + 1])
        return (
          <MessageBubble
            key={message.msgId}
            mine={mine}
            continued={continued}
            sender={mine || continued ? undefined : message.displayName || message.from}
            time={continued ? undefined : formatTime(message.ts)}
            fullTime={formatFullTime(message.ts)}
            showStatus={lastOfBurst}
            delivered={message.delivered}
            sending={message.sending}
          >
            {message.text}
          </MessageBubble>
        )
      })}
    </div>
  )
}
