import { formatTime } from '../../lib/format'
import type { ConversationSummary } from '../../types/domain'
import Avatar from '../common/Avatar'
import Badge from '../common/Badge'
import './ConversationItem.css'

type ConversationItemProps = {
  item: ConversationSummary
  selected?: boolean
  onSelect: (item: ConversationSummary) => void
}

export default function ConversationItem({ item, selected = false, onSelect }: ConversationItemProps) {
  return (
    <button
      className="conversation-item interactive"
      type="button"
      aria-current={selected || undefined}
      onClick={() => onSelect(item)}
    >
      <Avatar name={item.name} online={item.online} />
      <span className="grow">
        <span className="conversation-item__line">
          <strong className="truncate">{item.name}</strong>
          {item.ts !== undefined && <small>{formatTime(item.ts)}</small>}
        </span>
        <span className="conversation-item__line">
          <small className="truncate">{item.preview}</small>
          {item.unread > 0 && <Badge count={item.unread} />}
        </span>
      </span>
    </button>
  )
}
