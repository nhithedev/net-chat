import type { ReactNode } from 'react'
import Avatar from '../common/Avatar'
import './MessageBubble.css'

type MessageBubbleProps = {
  children: ReactNode
  mine?: boolean
  continued?: boolean // tin tiếp theo trong cụm: ẩn avatar, sát tin trước
  sender?: string
  time?: string
  fullTime?: string // hiện khi hover, đến giây
  showStatus?: boolean
  delivered?: boolean
  sending?: boolean
}

export default function MessageBubble({
  children,
  mine = false,
  continued = false,
  sender,
  time,
  fullTime,
  showStatus = true,
  delivered = true,
  sending = false,
}: MessageBubbleProps) {
  const pending = sending || !delivered
  return (
    <div className="bubble-row" data-mine={mine || undefined} data-continued={continued || undefined}>
      {!mine && (continued ? <span className="bubble-row__gap" /> : <Avatar name={sender} size="small" />)}
      <div className="bubble-row__content">
        {(sender || time) && (
          <div className="bubble-row__meta">
            {sender && <strong>{sender}</strong>}
            {time && <small>{time}</small>}
          </div>
        )}
        <div className="bubble-wrap">
          <div className="bubble">{children}</div>
          {fullTime && <span className="bubble-wrap__time" role="tooltip">{fullTime}</span>}
        </div>
        {showStatus && (
          <small className="bubble-row__delivery" data-pending={pending || undefined}>
            {sending ? 'Đang gửi' : delivered ? 'Đã gửi' : 'Chưa gửi'}
          </small>
        )}
      </div>
    </div>
  )
}
