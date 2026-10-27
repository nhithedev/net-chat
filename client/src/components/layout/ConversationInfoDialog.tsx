import { useChat } from '../../hooks/ChatContext'
import Dialog from '../common/Dialog'
import FriendProfile from '../friends/FriendProfile'
import GroupInfoPanel from '../groups/GroupInfoPanel'

type ConversationInfoDialogProps = {
  currentUser: string
  onInvite: () => void
}

// Popup thông tin bạn bè hoặc nhóm. Độc lập với hội thoại đang mở:
// chỉ khi bấm "Nhắn tin" trong popup mới chuyển sang chat.
export default function ConversationInfoDialog({ currentUser, onInvite }: ConversationInfoDialogProps) {
  const { state, actions } = useChat()
  const target = state.infoTarget
  if (!target) return null

  const isFriend = (username: string) => state.friends.some((friend) => friend.username === username)

  if (target.kind === 'group') {
    const group = state.groups.find((item) => item.groupId === target.groupId)
    if (!group) return null
    return (
      <Dialog open title="Thông tin nhóm" size="lg" onClose={actions.closeInfo}>
        <GroupInfoPanel
          group={group}
          members={state.members[group.groupId] ?? []}
          currentUser={currentUser}
          isFriend={isFriend}
          onInvite={onInvite}
          onKick={actions.kickMember}
          onLeave={actions.leaveGroup}
        />
      </Dialog>
    )
  }

  const friend =
    state.friends.find((item) => item.username === target.username) ??
    state.userDirectory.find((item) => item.username === target.username)
  if (!friend) return null

  return (
    <Dialog open title="Thông tin người dùng" size="lg" onClose={actions.closeInfo}>
      <FriendProfile
        friend={friend}
        isFriend={isFriend(friend.username)}
        onMessage={() => actions.openFriend(friend)}
        onRemove={actions.removeFriend}
        onBlock={actions.blockFriend}
      />
    </Dialog>
  )
}
