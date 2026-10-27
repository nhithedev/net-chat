import type { Conversation } from '../../types/domain'
import GroupInviteActions from '../groups/GroupInviteActions'
import ChatHeader from './ChatHeader'
import MessageInput from './MessageInput'
import MessageList from './MessageList'
import PeerStatusBanner from './PeerStatusBanner'
import './ChatView.css'

type ChatViewProps = {
  conversation: Conversation
  currentUser: string
  onSend: (text: string) => void
  onClose: () => void
  onInfo: () => void
  onAcceptInvite: () => void
  onDeclineInvite: () => void
}

export default function ChatView({
  conversation,
  currentUser,
  onSend,
  onClose,
  onInfo,
  onAcceptInvite,
  onDeclineInvite,
}: ChatViewProps) {
  return (
    <section className="chat-view">
      <ChatHeader
        title={conversation.title}
        subtitle={conversation.subtitle}
        online={conversation.kind === 'dm' ? conversation.online : undefined}
        kind={conversation.kind}
        onClose={onClose}
        onInfo={onInfo}
      />
      <PeerStatusBanner name={conversation.title} status={conversation.peerStatus} />
      <MessageList messages={conversation.messages} currentUser={currentUser} loading={conversation.loading} />
      {conversation.kind === 'groupInvite' ? (
        <GroupInviteActions groupName={conversation.title} onAccept={onAcceptInvite} onDecline={onDeclineInvite} />
      ) : (
        <MessageInput onSend={onSend} />
      )}
    </section>
  )
}
