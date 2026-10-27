import { useState } from 'react'
import { presenceLabel } from '../../lib/format'
import type { Friend, Relation } from '../../types/domain'
import Avatar from '../common/Avatar'
import Button from '../common/Button'
import Dialog from '../common/Dialog'
import Hero from '../common/Hero'
import RelationBadge from '../common/RelationBadge'
import './FriendProfile.css'

type FriendProfileProps = {
  friend: Friend
  relation: Relation
  onMessage: () => void
  onRemove: (friend: Friend) => void
  onBlock: (friend: Friend) => void
  onSendRequest: (friend: Friend) => void
  onCancelRequest: (friend: Friend) => void
  onAcceptRequest: (friend: Friend) => void
}

export default function FriendProfile({
  friend,
  relation,
  onMessage,
  onRemove,
  onBlock,
  onSendRequest,
  onCancelRequest,
  onAcceptRequest,
}: FriendProfileProps) {
  const [confirming, setConfirming] = useState(false)

  function remove() {
    onRemove(friend)
    setConfirming(false)
  }

  return (
    <div className="friend-profile">
      <Hero
        avatar={<Avatar name={friend.displayName} src={friend.avatarUrl} size="large" online={friend.online} />}
        title={friend.displayName}
        subtitle={`@${friend.username}`}
      >
        <p className="subtle">{presenceLabel(friend)}</p>
        <RelationBadge isFriend={relation === 'friend'} />
        {friend.bio && <p>{friend.bio}</p>}
      </Hero>
      <div className="friend-profile__footer">
        <div className="divider" />
        <div className="friend-profile__actions">
          <Button onClick={onMessage}>Nhắn tin</Button>
          {relation === 'friend' && <Button variant="outline" onClick={() => setConfirming(true)}>Hủy kết bạn</Button>}
          {relation === 'none' && <Button variant="soft" onClick={() => onSendRequest(friend)}>Gửi kết bạn</Button>}
          {relation === 'pending_out' && <Button variant="outline" onClick={() => onCancelRequest(friend)}>Hủy lời mời</Button>}
          {relation === 'pending_in' && <Button variant="soft" onClick={() => onAcceptRequest(friend)}>Đồng ý kết bạn</Button>}
          <Button variant="danger" onClick={() => onBlock(friend)}>Chặn</Button>
        </div>
      </div>
      <Dialog
        open={confirming}
        title="Hủy kết bạn?"
        onClose={() => setConfirming(false)}
        actions={
          <>
            <Button variant="ghost" onClick={() => setConfirming(false)}>Giữ lại</Button>
            <Button onClick={remove}>Xác nhận</Button>
          </>
        }
      >
        <p className="subtle">Bạn và {friend.displayName} sẽ không còn trong danh sách bạn bè.</p>
      </Dialog>
    </div>
  )
}
