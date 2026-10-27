import type { User } from '../../types/domain'
import Avatar from '../common/Avatar'
import Button from '../common/Button'
import ListRow from '../common/ListRow'

type FriendRequestCardProps = {
  user: User
  outgoing?: boolean
  onAccept?: () => void
  onReject?: () => void
  onCancel?: () => void
}

// protocol.md §5.3: pending_in/pending_out chỉ trả username; displayName do hook tra từ directory.
export default function FriendRequestCard({ user, outgoing = false, onAccept, onReject, onCancel }: FriendRequestCardProps) {
  return (
    <ListRow
      avatar={<Avatar name={user.displayName} />}
      title={user.displayName}
      subtitle={`@${user.username}`}
      trailing={
        outgoing ? (
          <Button variant="outline" size="sm" onClick={onCancel}>Hủy lời mời</Button>
        ) : (
          <div className="row" data-gap="1">
            <Button shape="icon" variant="ghost" icon="close" ariaLabel="Từ chối lời mời" onClick={onReject} />
            <Button shape="icon" variant="soft" icon="check" ariaLabel="Chấp nhận lời mời" onClick={onAccept} />
          </div>
        )
      }
    />
  )
}
