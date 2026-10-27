import type { ConversationSummary } from '../../types/domain'
import ConversationItem from './ConversationItem'

type ConversationListProps = {
  conversations: ConversationSummary[]
  activeKey: string | null
  onSelect: (item: ConversationSummary) => void
}

export default function ConversationList({ conversations, activeKey, onSelect }: ConversationListProps) {
  return (
    <div className="stack" data-gap="1">
      {conversations.map((item) => (
        <ConversationItem key={item.key} item={item} selected={item.key === activeKey} onSelect={onSelect} />
      ))}
    </div>
  )
}
