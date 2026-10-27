import Button from '../common/Button'
import './MessageMenu.css'

type MessageMenuProps = {
  onReply: () => void
  onEdit: () => void
  onReact: () => void
  onDelete: () => void
}

// ext: schema.sql có reply_to_id / edited_at / deleted_at / message_reactions, protocol.md chưa có.
export default function MessageMenu({ onReply, onEdit, onReact, onDelete }: MessageMenuProps) {
  const items: Array<[string, () => void]> = [
    ['Trả lời', onReply],
    ['Sửa', onEdit],
    ['Reaction', onReact],
    ['Xóa', onDelete],
  ]
  return (
    <div className="message-menu" role="menu">
      {items.map(([label, action]) => (
        <Button key={label} role="menuitem" variant="ghost" size="sm" onClick={action}>
          {label}
        </Button>
      ))}
    </div>
  )
}
