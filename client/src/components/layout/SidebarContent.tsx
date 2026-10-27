import { useChat } from '../../hooks/ChatContext'
import { matchesQuery } from '../../lib/list'
import type { Tab } from '../../types/domain'
import ConversationList from '../chat/ConversationList'
import EmptyState from '../common/EmptyState'
import FriendList from '../friends/FriendList'
import GroupList from '../groups/GroupList'

const emptyContent: Record<Tab, { title: string; text: string }> = {
  messages: { title: 'Chưa có cuộc trò chuyện', text: 'Kết bạn để bắt đầu nhắn tin.' },
  friends: { title: 'Chưa có bạn bè', text: 'Thêm bạn để cùng trò chuyện.' },
  groups: { title: 'Chưa tham gia nhóm', text: 'Tạo nhóm hoặc chấp nhận một lời mời.' },
}

// Chọn danh sách theo tab (F-UI-2), lọc theo tên khi có từ khóa; tab rỗng thì hiện trạng thái rỗng.
export default function SidebarContent() {
  const { state, actions } = useChat()
  const query = state.search.trim()
  const byName = <T extends { username: string; displayName: string }>(list: T[]) =>
    list.filter((user) => matchesQuery(query, user.displayName, user.username))

  const noResults = <EmptyState compact title="Không tìm thấy" text={`Không có kết quả cho “${query}”.`} />

  if (state.tab === 'friends' && (state.friends.length || state.pendingIn.length || state.pendingOut.length || state.onlineUsers.length)) {
    const friends = byName(state.friends)
    const pendingIn = byName(state.pendingIn)
    const pendingOut = byName(state.pendingOut)
    const onlineUsers = byName(state.onlineUsers)
    const shown = { all: friends, requests: pendingIn, sent: pendingOut, online: onlineUsers }[state.friendFilter]
    if (query && shown.length === 0) return noResults
    return (
      <FriendList
        view={state.friendFilter}
        friends={friends}
        pendingIn={pendingIn}
        pendingOut={pendingOut}
        onlineUsers={onlineUsers}
        isFriend={(username) => state.relationOf(username) === 'friend'}
        onMessage={actions.openFriend}
        onInfo={actions.openFriendInfo}
        onAccept={actions.acceptFriend}
        onReject={actions.rejectFriend}
        onCancel={actions.cancelFriendRequest}
      />
    )
  }

  if (state.tab === 'groups' && (state.groups.length || state.invites.length)) {
    const groups = state.groups.filter((group) => matchesQuery(query, group.name))
    const invites = state.invites.filter((invite) => matchesQuery(query, invite.name))
    if (query && groups.length + invites.length === 0) return noResults
    return (
      <GroupList
        groups={groups}
        invites={invites}
        activeInviteId={state.activeInviteId}
        onSelect={actions.openGroup}
        onSelectInvite={actions.openGroupInvite}
      />
    )
  }

  if (state.tab === 'messages' && state.conversations.length) {
    const conversations = state.conversations.filter((item) => matchesQuery(query, item.name))
    if (query && conversations.length === 0) return noResults
    return <ConversationList conversations={conversations} activeKey={state.activeKey} onSelect={actions.openConversation} />
  }

  return <EmptyState compact {...emptyContent[state.tab]} />
}
