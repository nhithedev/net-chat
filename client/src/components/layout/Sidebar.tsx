import { useChat } from '../../hooks/ChatContext'
import type { Account } from '../../types/domain'
import Avatar from '../common/Avatar'
import Button from '../common/Button'
import TextField from '../common/TextField'
import FriendFilterMenu from './FriendFilterMenu'
import SidebarContent from './SidebarContent'
import SidebarTabs from './SidebarTabs'
import './Sidebar.css'

type SidebarProps = {
  user: Account
  online: boolean
  onSettings: () => void
  onAddFriend: () => void
  onCreateGroup: () => void
}

export default function Sidebar({ user, online, onSettings, onAddFriend, onCreateGroup }: SidebarProps) {
  const { state, actions } = useChat()
  const unreadMessages = state.conversations.reduce((total, item) => total + item.unread, 0)

  return (
    <aside className="sidebar">
      <header className="row">
        <strong className="sidebar__brand grow">net-chat</strong>
        <button className="sidebar__account interactive" type="button" aria-label="Mở tài khoản của tôi" onClick={onSettings}>
          <Avatar name={user.displayName} src={user.avatarUrl} online={online} />
        </button>
      </header>
      <SidebarTabs
        active={state.tab}
        unread={{ messages: unreadMessages, friends: state.pendingIn.length, groups: state.invites.length }}
        onChange={actions.setTab}
      />
      <div className="sidebar__search row" data-gap="2">
        <div className="grow">
          <TextField
            aria-label="Tìm kiếm theo tên"
            placeholder="Tìm theo tên..."
            value={state.search}
            onChange={(event) => actions.setSearch(event.target.value)}
          />
        </div>
        {state.tab === 'friends' && (
          <FriendFilterMenu
            active={state.friendFilter}
            counts={{ requests: state.pendingIn.length, sent: state.pendingOut.length, online: state.onlineUsers.length }}
            onChange={actions.setFriendFilter}
          />
        )}
        {state.tab === 'friends' && <Button shape="pill" onClick={onAddFriend}>+ Thêm bạn</Button>}
        {state.tab === 'groups' && <Button shape="pill" onClick={onCreateGroup}>+ Tạo nhóm</Button>}
      </div>
      <div className="sidebar__content">
        <SidebarContent />
      </div>
    </aside>
  )
}
