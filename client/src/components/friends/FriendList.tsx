import type { Friend, FriendFilter } from '../../types/domain'
import Avatar from '../common/Avatar'
import Button from '../common/Button'
import EmptyState from '../common/EmptyState'
import ListRow from '../common/ListRow'
import FriendRequestCard from './FriendRequestCard'
import OnlineList from './OnlineList'

type FriendListProps = {
  view: FriendFilter
  friends: Friend[]
  pendingIn: Friend[]
  pendingOut: Friend[]
  onlineUsers: Friend[]
  isFriend: (username: string) => boolean
  onMessage: (friend: Friend) => void
  onInfo: (friend: Friend) => void
  onAccept: (user: Friend) => void
  onReject: (user: Friend) => void
  onCancel: (user: Friend) => void
}

export default function FriendList({
  view,
  friends,
  pendingIn,
  pendingOut,
  onlineUsers,
  isFriend,
  onMessage,
  onInfo,
  onAccept,
  onReject,
  onCancel,
}: FriendListProps) {
  if (view === 'online') return <OnlineList users={onlineUsers} isFriend={isFriend} onSelect={onInfo} />

  if (view === 'requests') {
    return (
      <ul className="stack" data-gap="1">
        {pendingIn.map((user) => (
          <FriendRequestCard key={user.username} user={user} onAccept={() => onAccept(user)} onReject={() => onReject(user)} />
        ))}
        {pendingIn.length === 0 && <EmptyState compact icon={null} text="Không có lời mời mới." />}
      </ul>
    )
  }

  if (view === 'sent') {
    return (
      <ul className="stack" data-gap="1">
        {pendingOut.map((user) => (
          <FriendRequestCard key={user.username} user={user} outgoing onCancel={() => onCancel(user)} />
        ))}
        {pendingOut.length === 0 && <EmptyState compact icon={null} text="Chưa gửi lời mời nào." />}
      </ul>
    )
  }

  return (
    <ul className="stack" data-gap="1">
      {friends.map((friend) => (
        <ListRow
          key={friend.username}
          avatar={<Avatar name={friend.displayName} online={friend.online} />}
          title={friend.displayName}
          trailing={
            <div className="row" data-gap="1">
              <Button shape="icon" variant="ghost" icon="message" ariaLabel={`Nhắn tin với ${friend.displayName}`} onClick={() => onMessage(friend)} />
              <Button shape="icon" variant="ghost" icon="info" ariaLabel={`Thông tin ${friend.displayName}`} onClick={() => onInfo(friend)} />
            </div>
          }
        />
      ))}
      {friends.length === 0 && <EmptyState compact icon={null} text="Chưa có bạn bè." />}
    </ul>
  )
}
