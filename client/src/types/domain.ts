// Kiểu dữ liệu phía giao diện, bám schema.sql và protocol.md.
// UI dùng camelCase; protocol dùng snake_case (display_name...).
// TODO: lớp src/api/ sẽ đổi snake_case <-> camelCase khi nối server thật.

import type { ErrorCodeValue } from './protocol'

export interface User {
  username: string
  displayName: string
  avatarUrl?: string // ext: users.avatar_url, chưa có trong protocol.md
  bio?: string // ext: users.bio, chưa có trong protocol.md
}

export interface Friend extends User {
  online: boolean
  lastSeenAt?: number // ext: users.last_seen_at (epoch ms), chỉ có ý nghĩa khi offline
}

// Quan hệ của người dùng hiện tại với một user khác (friendships + friend_requests).
export type Relation = 'friend' | 'pending_out' | 'pending_in' | 'none'

// Chỉ dùng cho mock đăng nhập: server thật không bao giờ trả mật khẩu.
export interface Account extends User {
  password: string
}

// conversations.id của type = 'group'.
export interface Group {
  groupId: number
  name: string
  owner: string
}

export type GroupMember = Friend

// Số thành viên suy ra từ GROUP_MEMBERS, không có trong GROUP_LIST.
export interface GroupView extends Group {
  memberCount: number
}

export interface GroupInvite {
  groupId: number
  name: string
  from: string // display_name người mời
  fromUsername: string
}

// number = msg_id do server cấp; string = id tạm phía client (tin đang gửi, tin hệ thống).
export type MessageId = number | string

export interface UserMessage {
  system?: false
  msgId: MessageId
  from: string
  displayName?: string
  text: string
  ts: number // Unix epoch mili-giây
  delivered: boolean
  sending?: boolean
}

export interface SystemMessage {
  system: true
  msgId: MessageId
  text: string
}

export type ChatMessage = UserMessage | SystemMessage

export type PeerStatus = 'ok' | 'disconnected'

interface ConversationBase {
  key: string
  title: string
  subtitle?: string
  peerStatus: PeerStatus
  unread: number // ext: suy ra từ conversation_members.last_read_msg_id
  loading?: boolean
  messages: ChatMessage[]
}

export interface DmConversation extends ConversationBase {
  kind: 'dm'
  username: string
  online: boolean
}

export interface GroupConversation extends ConversationBase {
  kind: 'group'
  groupId: number
}

export interface GroupInviteConversation extends ConversationBase {
  kind: 'groupInvite'
  groupId: number
}

export type Conversation = DmConversation | GroupConversation | GroupInviteConversation

export interface ConversationSummary {
  key: string
  name: string
  preview: string
  ts?: number
  unread: number
  online?: boolean // chỉ có ở hội thoại 1-1
}

// activity_logs.event (schema.sql) theo danh sách sự kiện ở spec.md F-LOG-1; không ghi nội dung tin.
export type ActivityEvent =
  | 'connect'
  | 'register'
  | 'login_ok'
  | 'login_fail'
  | 'logout'
  | 'disconnect'
  | 'timeout'
  | 'protocol_error'
  | 'friend_request'
  | 'friend_accept'
  | 'friend_reject'
  | 'friend_cancel'
  | 'friend_remove'
  | 'group_create'
  | 'group_invite'
  | 'group_join'
  | 'group_leave'
  | 'group_kick'
  | 'msg_send'
  | 'group_msg_send'

export type LogDetail = Record<string, string | number> // activity_logs.detail (jsonb)

export interface ActivityLogEntry {
  id: number
  ts: number // activity_logs.ts, epoch ms
  connId?: number // activity_logs.conn_id (do Server C cấp)
  username?: string // activity_logs.user_id đã đổi sang username; thiếu khi chưa xác thực
  event: ActivityEvent
  detail?: LogDetail
}

export type Tab = 'messages' | 'friends' | 'groups'
export type FriendFilter = 'all' | 'requests' | 'sent' | 'online'
export type ConnectionStatus = 'connecting' | 'open' | 'reconnecting' | 'restored' | 'closed'

export interface Toast {
  id: string
  kind: 'info' | 'error'
  text: string
}

export type ActionResult =
  | { ok: true }
  | { ok: false; code?: ErrorCodeValue | 'self'; error?: string }

export type PasswordChangeResult =
  | { ok: true }
  | { ok: false; code: 'wrong_password' | 'weak_password' | 'mismatch' }
