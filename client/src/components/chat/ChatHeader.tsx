import type { Conversation } from '../../types/domain'
import Avatar from '../common/Avatar'
import Button from '../common/Button'

type ChatHeaderProps = {
  title: string
  subtitle?: string
  online?: boolean
  kind: Conversation['kind']
  onInfo: () => void
  onClose: () => void
}

export default function ChatHeader({ title, subtitle, online, kind, onInfo, onClose }: ChatHeaderProps) {
  const infoLabel = kind === 'group' ? 'Thông tin nhóm' : 'Thông tin người dùng'
  return (
    <header className="chat-header row">
      <Avatar name={title} online={online} />
      <div className="grow">
        <h2 className="truncate">{title}</h2>
        {subtitle ? (
          <p className="subtle truncate">{subtitle}</p>
        ) : (
          // Chấm trạng thái đã có trên avatar, ở đây chỉ ghi chữ để không bị lặp.
          <p className="subtle">{online ? 'Đang online' : 'Ngoại tuyến'}</p>
        )}
      </div>
      {kind !== 'groupInvite' && (
        <>
          <Button shape="icon" variant="ghost" icon="info" ariaLabel={infoLabel} onClick={onInfo} />
          <Button variant="outline" onClick={onClose}>Ngắt kết nối</Button>
        </>
      )}
    </header>
  )
}
