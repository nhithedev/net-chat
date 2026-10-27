import { useState } from 'react'
import ChatView from '../components/chat/ChatView'
import EmptyState from '../components/common/EmptyState'
import ToastHost from '../components/common/ToastHost'
import AddFriendDialog from '../components/friends/AddFriendDialog'
import CreateGroupDialog from '../components/groups/CreateGroupDialog'
import InviteToGroupDialog from '../components/groups/InviteToGroupDialog'
import KickedNotice from '../components/groups/KickedNotice'
import ConversationInfoDialog from '../components/layout/ConversationInfoDialog'
import GlobalStatusArea from '../components/layout/GlobalStatusArea'
import Sidebar from '../components/layout/Sidebar'
import { ChatContext } from '../hooks/ChatContext'
import useChatPrototype from '../hooks/useChatPrototype'
import useConnectionStatus from '../hooks/useConnectionStatus'
import type { Account, User } from '../types/domain'
import type { KickReason } from '../types/protocol'
import './ChatPage.css'

type ChatPageProps = {
  user: Account
  onSessionExpired: () => void
  onOpenAccount: () => void
  onKicked: (reason: KickReason) => void
  onDisconnected: (reason: KickReason) => void
  onBlockUser: (user: User) => void
}

type DialogName = 'addFriend' | 'createGroup' | 'invite'

export default function ChatPage({ user, onSessionExpired, onOpenAccount, onKicked, onDisconnected, onBlockUser }: ChatPageProps) {
  const connection = useConnectionStatus({ onSessionExpired, onDisconnected })
  const chat = useChatPrototype(user, connection.status, { onKicked, onDisconnected, onBlockUser })
  const [dialog, setDialog] = useState<DialogName | null>(null)
  const { state, actions } = chat
  const closeDialog = () => setDialog(null)

  return (
    <ChatContext.Provider value={chat}>
      <main className="chat-shell">
        <GlobalStatusArea connectionStatus={connection.status} offlineCount={state.offlineCount} offlineHasMore={state.offlineHasMore} />
        <div className="chat-page" data-conversation={state.activeConversation ? '' : undefined}>
          <Sidebar
            user={user}
            online={connection.status === 'open' || connection.status === 'restored'}
            onSettings={onOpenAccount}
            onAddFriend={() => setDialog('addFriend')}
            onCreateGroup={() => setDialog('createGroup')}
          />
          <section className="chat-page__main">
            {state.activeConversation ? (
              <ChatView
                conversation={state.activeConversation}
                currentUser={user.username}
                onSend={actions.sendMessage}
                onClose={actions.closeConversation}
                onInfo={actions.openInfo}
                onAcceptInvite={actions.acceptGroupInvite}
                onDeclineInvite={actions.declineGroupInvite}
              />
            ) : (
              <EmptyState title="Chọn một cuộc trò chuyện" text="Chọn bạn bè hoặc nhóm từ thanh bên để bắt đầu." />
            )}
          </section>
        </div>
        <ConversationInfoDialog currentUser={user.username} onInvite={() => setDialog('invite')} />
        <AddFriendDialog open={dialog === 'addFriend'} users={state.userDirectory} onClose={closeDialog} onSubmit={actions.sendFriendRequest} />
        <CreateGroupDialog open={dialog === 'createGroup'} onClose={closeDialog} onSubmit={actions.createGroup} />
        <InviteToGroupDialog open={dialog === 'invite'} users={state.inviteCandidates} full={state.groupFull} onClose={closeDialog} onSubmit={actions.inviteToGroup} />
        {state.kickedGroup && <KickedNotice groupName={state.kickedGroup.name} onClose={actions.clearKickedNotice} />}
        <ToastHost toasts={state.toasts} onDismiss={actions.dismissToast} />
      </main>
    </ChatContext.Provider>
  )
}
