import { useMemo, useRef, useState } from 'react'
import { addUnique, removeBy } from '../lib/list'
import {
  demoConversations,
  demoFriends,
  demoGroups,
  demoInvites,
  demoMembers,
  demoOfflineCount,
  demoPendingIn,
  demoToasts,
  userDirectory,
} from '../mocks/chat'
import { ErrorCode, Limits, type ErrorCodeValue, type KickReason } from '../types/protocol'
import type {
  Account,
  ActionResult,
  ChatMessage,
  ConnectionStatus,
  Conversation,
  ConversationSummary,
  Friend,
  FriendFilter,
  Group,
  GroupInvite,
  GroupInviteConversation,
  GroupMember,
  GroupView,
  SystemMessage,
  Tab,
  User,
  UserMessage,
} from '../types/domain'
import useToasts from './useToasts'

type Callbacks = {
  onKicked?: (reason: KickReason) => void
  onDisconnected?: (reason: KickReason) => void
  onBlockUser?: (user: User) => void
}

// Popup thông tin độc lập với hội thoại đang mở: xem info không làm đổi hội thoại.
export type InfoTarget = { kind: 'user'; username: string } | { kind: 'group'; groupId: number }

type ConversationPatch = Partial<Pick<Conversation, 'unread' | 'loading' | 'messages'>>
type OfflineBatch = { key: string; messages: UserMessage[] }

const LOAD_HISTORY_MS = 500
const SEND_ACK_MS = 700
const errorMessages: Partial<Record<ErrorCodeValue, string>> = {
  [ErrorCode.RateLimit]: 'Bạn gửi quá nhanh.',
  [ErrorCode.TooLarge]: 'Nội dung gửi vượt quá giới hạn.',
  [ErrorCode.Internal]: 'Máy chủ đang gặp sự cố. Vui lòng thử lại.',
}

let localCounter = 0
const localId = (prefix: string) => `${prefix}-${Date.now()}-${++localCounter}`

const dmKey = (username: string) => `dm:${username}`
const groupKey = (groupId: number) => `group:${groupId}`
const systemMessage = (text: string): SystemMessage => ({ system: true, msgId: localId('system'), text })

function lastUserMessage(conversation: Conversation): UserMessage | undefined {
  return conversation.messages.findLast((message): message is UserMessage => !message.system)
}

function buildInviteConversation(invite: GroupInvite): GroupInviteConversation {
  return {
    key: `invite:${invite.groupId}`,
    kind: 'groupInvite',
    groupId: invite.groupId,
    title: invite.name,
    subtitle: `${invite.from} đã mời bạn`,
    peerStatus: 'ok',
    unread: 0,
    messages: [
      systemMessage(`${invite.from} đã gửi lời mời vào nhóm`),
      {
        msgId: localId('invite'),
        from: invite.fromUsername,
        displayName: invite.from,
        text: 'Tham gia cùng mọi người nhé. Các tin nhắn mới sẽ xuất hiện sau khi bạn chấp nhận.',
        ts: Date.now(),
        delivered: true,
      },
    ],
  }
}

export default function useChatPrototype(
  currentUser: Account,
  connectionStatus: ConnectionStatus = 'open',
  { onKicked, onDisconnected, onBlockUser }: Callbacks = {},
) {
  const hasDemoData = currentUser.username === 'demo'
  const demo = <T,>(value: T, empty: T) => (hasDemoData ? value : empty)

  const [tab, setTab] = useState<Tab>('messages')
  const [search, setSearch] = useState('')
  const [friendFilter, setFriendFilter] = useState<FriendFilter>('all')
  const [activeKey, setActiveKey] = useState<string | null>(null)
  const [activeInvite, setActiveInvite] = useState<GroupInvite | null>(null)
  const [infoTarget, setInfoTarget] = useState<InfoTarget | null>(null)
  const [friends, setFriends] = useState<Friend[]>(() => demo(demoFriends, []))
  const [pendingIn, setPendingIn] = useState<Friend[]>(() => demo(demoPendingIn, []))
  const [pendingOut, setPendingOut] = useState<Friend[]>([])
  const [groups, setGroups] = useState<Group[]>(() => demo(demoGroups, []))
  const [members, setMembers] = useState<Record<number, GroupMember[]>>(() => demo(demoMembers, {}))
  const [conversations, setConversations] = useState<Record<string, Conversation>>(() => demo(demoConversations, {}))
  const [invites, setInvites] = useState<GroupInvite[]>(() => demo(demoInvites, []))
  const [kickedGroup, setKickedGroup] = useState<Group | null>(null)
  const [presenceUsers, setPresenceUsers] = useState<Friend[]>(() =>
    userDirectory.filter((user) => user.username !== currentUser.username),
  )
  const [offlineCount, setOfflineCount] = useState(() => demo(demoOfflineCount, 0))
  const [offlineHasMore, setOfflineHasMore] = useState(hasDemoData)
  const { toasts, addToast, dismissToast } = useToasts(hasDemoData ? demoToasts : [])
  const nextGroupId = useRef(1000) // id tạm đến khi server cấp group_id

  const isOnline = (username: string, fallback = false) =>
    presenceUsers.find((user) => user.username === username)?.online ?? fallback

  const inviteConversation = useMemo(
    () => (activeInvite ? buildInviteConversation(activeInvite) : null),
    [activeInvite],
  )

  const storedConversation = activeKey ? conversations[activeKey] ?? null : null
  const baseConversation = inviteConversation ?? storedConversation
  let activeConversation: Conversation | null = baseConversation
  if (baseConversation?.kind === 'group') {
    activeConversation = { ...baseConversation, subtitle: `${members[baseConversation.groupId]?.length ?? 0} thành viên` }
  } else if (baseConversation?.kind === 'dm') {
    activeConversation = { ...baseConversation, online: isOnline(baseConversation.username, baseConversation.online) }
  }

  const groupViews: GroupView[] = groups.map((group) => ({
    ...group,
    memberCount: members[group.groupId]?.length ?? 0,
  }))

  const conversationList: ConversationSummary[] = Object.values(conversations).map((conversation) => {
    const last = lastUserMessage(conversation)
    return {
      key: conversation.key,
      name: conversation.title,
      preview: last?.text || 'Chưa có tin nhắn',
      ts: last?.ts,
      unread: conversation.unread,
      online: conversation.kind === 'dm' ? isOnline(conversation.username, conversation.online) : undefined,
    }
  })

  const activeGroupId = activeConversation?.kind === 'group' ? activeConversation.groupId : null
  const activeGroupMembers = activeGroupId ? members[activeGroupId] ?? [] : []
  const inviteCandidates = presenceUsers
    .filter((user) => !activeGroupMembers.some((member) => member.username === user.username))
    .map((user) => ({ ...user, isFriend: friends.some((friend) => friend.username === user.username) }))

  // ---- helpers cập nhật state -------------------------------------------

  function patchConversation(key: string, patch: ConversationPatch | ((conversation: Conversation) => ConversationPatch)) {
    setConversations((current) => {
      const conversation = current[key]
      if (!conversation) return current
      return { ...current, [key]: { ...conversation, ...(typeof patch === 'function' ? patch(conversation) : patch) } }
    })
  }

  function appendMessage(key: string, message: ChatMessage) {
    patchConversation(key, (conversation) => ({ messages: [...conversation.messages, message] }))
  }

  // Người dùng chuyển tab: ô tìm kiếm áp dụng cho từng tab nên xóa từ khóa.
  function changeTab(next: Tab) {
    setTab(next)
    setSearch('')
  }

  function leaveToTab(next: Tab) {
    setActiveKey(null)
    setInfoTarget(null)
    setTab(next)
  }

  // Rời danh sách bạn bè sau khi hủy kết bạn/chặn; chỉ đóng chat nếu đang chat với chính người đó.
  function afterFriendRemoved(username: string) {
    setInfoTarget(null)
    setTab('friends')
    if (activeKey === dmKey(username)) setActiveKey(null)
  }

  function openConversationKey(key: string, seed?: Conversation) {
    const conversation = conversations[key] ?? seed
    if (!conversation) return
    acknowledgeOffline(conversation.unread)
    setConversations((current) => ({ ...current, [key]: { ...(current[key] ?? conversation), unread: 0, loading: true } }))
    setActiveInvite(null)
    setActiveKey(key)
    setInfoTarget(null)
    setTab('messages')
    window.setTimeout(() => patchConversation(key, { loading: false }), LOAD_HISTORY_MS)
  }

  // ---- hội thoại ---------------------------------------------------------

  function openConversation(item: ConversationSummary) {
    openConversationKey(item.key)
  }

  function openFriend(friend: Friend) {
    openConversationKey(dmKey(friend.username), {
      key: dmKey(friend.username),
      kind: 'dm',
      title: friend.displayName,
      username: friend.username,
      online: friend.online,
      peerStatus: friend.online ? 'ok' : 'disconnected',
      unread: 0,
      messages: [],
    })
  }

  // Chỉ mở popup; người dùng chọn "Nhắn tin" trong popup mới chuyển sang chat.
  function openFriendInfo(friend: Friend) {
    setInfoTarget({ kind: 'user', username: friend.username })
  }

  function openInfo() {
    if (activeConversation?.kind === 'dm') setInfoTarget({ kind: 'user', username: activeConversation.username })
    else if (activeConversation?.kind === 'group') setInfoTarget({ kind: 'group', groupId: activeConversation.groupId })
  }

  function openGroup(group: Group) {
    openConversationKey(groupKey(group.groupId), {
      key: groupKey(group.groupId),
      kind: 'group',
      groupId: group.groupId,
      title: group.name,
      peerStatus: 'ok',
      unread: 0,
      messages: [],
    })
  }

  function openGroupInvite(invite: GroupInvite) {
    setActiveInvite(invite)
    setActiveKey(`invite:${invite.groupId}`)
    setInfoTarget(null)
    setTab('groups')
  }

  function closeConversation() {
    setActiveKey(null)
    setActiveInvite(null)
  }

  function sendMessage(text: string) {
    if (!activeKey || !conversations[activeKey]) return
    const key = activeKey
    const sending = connectionStatus === 'open'
    const message: UserMessage = {
      msgId: localId('local'),
      from: currentUser.username,
      text,
      ts: Date.now(),
      delivered: false,
      sending,
    }
    appendMessage(key, message)
    if (!sending) return
    window.setTimeout(() => {
      patchConversation(key, (conversation) => ({
        messages: conversation.messages.map((item) =>
          !item.system && item.msgId === message.msgId ? { ...item, delivered: true, sending: false } : item,
        ),
      }))
    }, SEND_ACK_MS)
  }

  // ---- bạn bè ------------------------------------------------------------

  function acceptFriend(user: Friend) {
    setPendingIn((current) => removeBy(current, (item) => item.username, user.username))
    setFriends((current) => addUnique(current, user, (item) => item.username))
    addToast(`${user.displayName} đã trở thành bạn bè.`)
  }

  function rejectFriend(user: Friend) {
    setPendingIn((current) => removeBy(current, (item) => item.username, user.username))
  }

  function receiveFriendRequest(user: Friend) {
    setPendingIn((current) => addUnique(current, user, (item) => item.username))
    addToast(`${user.displayName} đã gửi lời mời kết bạn.`)
  }

  function receiveFriendAccepted(user: Friend) {
    setPendingOut((current) => removeBy(current, (item) => item.username, user.username))
    setFriends((current) => addUnique(current, user, (item) => item.username))
    addToast(`${user.displayName} đã chấp nhận lời mời kết bạn.`)
  }

  function receiveFriendRejected(user: Friend) {
    setPendingOut((current) => removeBy(current, (item) => item.username, user.username))
    addToast(`${user.displayName} đã từ chối lời mời kết bạn.`)
  }

  // ext: schema.sql có friend_requests.status = 'cancelled', protocol.md chưa có lệnh hủy lời mời.
  function cancelFriendRequest(user: Friend) {
    setPendingOut((current) => removeBy(current, (item) => item.username, user.username))
    addToast(`Đã hủy lời mời kết bạn tới ${user.displayName}.`)
  }

  function receiveFriendCancelled(user: Friend) {
    setPendingIn((current) => removeBy(current, (item) => item.username, user.username))
  }

  function setUserPresence(username: string, online: boolean) {
    const setOnline = (list: Friend[]) => list.map((user) => (user.username === username ? { ...user, online } : user))
    setPresenceUsers(setOnline)
    setFriends(setOnline)
    const user = presenceUsers.find((item) => item.username === username)
    if (online && user) addToast(`${user.displayName} đang online.`)
  }

  function sendFriendRequest(username: string): ActionResult {
    const normalized = username.toLowerCase()
    if (normalized === currentUser.username.toLowerCase()) return { ok: false, code: 'self' }
    const user = userDirectory.find((item) => item.username.toLowerCase() === normalized)
    if (!user) return { ok: false, code: ErrorCode.NotFound }
    const known = [...friends, ...pendingIn, ...pendingOut]
    if (known.some((item) => item.username === user.username)) return { ok: false, code: ErrorCode.AlreadyExists }
    setPendingOut((current) => [...current, user])
    addToast(`Đã gửi lời mời kết bạn tới ${user.displayName}.`)
    return { ok: true }
  }

  function removeFriend(friend: Friend) {
    setFriends((current) => removeBy(current, (item) => item.username, friend.username))
    afterFriendRemoved(friend.username)
    addToast(`Đã hủy kết bạn với ${friend.displayName}.`)
  }

  function blockFriend(friend: Friend) {
    setFriends((current) => removeBy(current, (item) => item.username, friend.username))
    afterFriendRemoved(friend.username)
    onBlockUser?.(friend)
    addToast(`Đã chặn ${friend.displayName}.`)
  }

  // ---- nhóm --------------------------------------------------------------

  function addGroup(group: Group, groupMembers: GroupMember[], firstMessage: string) {
    const key = groupKey(group.groupId)
    setGroups((current) => [...current, group])
    setMembers((current) => ({ ...current, [group.groupId]: groupMembers }))
    setConversations((current) => ({
      ...current,
      [key]: {
        key,
        kind: 'group',
        groupId: group.groupId,
        title: group.name,
        peerStatus: 'ok',
        unread: 0,
        messages: [systemMessage(firstMessage)],
      },
    }))
    setActiveInvite(null)
    setActiveKey(key)
    setTab('messages')
  }

  const selfMember: GroupMember = { username: currentUser.username, displayName: currentUser.displayName, online: true }

  function createGroup(name: string): ActionResult {
    const cleanName = name.trim()
    if (cleanName.length < 1 || cleanName.length > Limits.groupNameMax) return { ok: false, error: 'Tên nhóm không hợp lệ.' }
    nextGroupId.current += 1
    addGroup(
      { groupId: nextGroupId.current, name: cleanName, owner: currentUser.username },
      [selfMember],
      `${currentUser.displayName} đã tạo nhóm`,
    )
    addToast(`Đã tạo nhóm ${cleanName}.`)
    return { ok: true }
  }

  function acceptGroupInvite() {
    if (!activeInvite) return
    const invite = activeInvite
    addGroup(
      { groupId: invite.groupId, name: invite.name, owner: invite.fromUsername },
      [{ username: invite.fromUsername, displayName: invite.from, online: true }, selfMember],
      `${currentUser.displayName} đã vào nhóm`,
    )
    setInvites((current) => removeBy(current, (item) => item.groupId, invite.groupId))
    setTab('groups')
    addToast(`Đã tham gia nhóm ${invite.name}.`)
  }

  function declineGroupInvite() {
    if (!activeInvite) return
    setInvites((current) => removeBy(current, (item) => item.groupId, activeInvite.groupId))
    closeConversation()
    addToast('Đã từ chối lời mời nhóm.')
  }

  function inviteToGroup(usernames: string[]): ActionResult {
    if (!activeGroupId || activeGroupMembers.length >= Limits.groupMembersMax) return { ok: false }
    const names = inviteCandidates
      .filter((candidate) => usernames.includes(candidate.username))
      .map((candidate) => candidate.displayName)
    if (!names.length) return { ok: false }
    addToast(`Đã gửi lời mời nhóm tới ${names.join(', ')}.`)
    return { ok: true }
  }

  function removeMember(groupId: number, user: User) {
    setMembers((current) => ({
      ...current,
      [groupId]: removeBy(current[groupId] ?? [], (item) => item.username, user.username),
    }))
    if (conversations[groupKey(groupId)]) appendMessage(groupKey(groupId), systemMessage(`${user.displayName} đã rời nhóm`))
  }

  function kickMember(member: GroupMember) {
    if (activeGroupId === null) return
    removeMember(activeGroupId, member)
    addToast(`Đã xóa ${member.displayName} khỏi nhóm.`)
  }

  function leaveGroup() {
    if (activeConversation?.kind !== 'group') return
    const { groupId, title } = activeConversation
    setGroups((current) => removeBy(current, (group) => group.groupId, groupId))
    leaveToTab('groups')
    addToast(`Đã rời nhóm ${title}.`)
  }

  function receiveGroupKicked(groupId: number) {
    const group = groups.find((item) => item.groupId === groupId)
    if (!group) return
    setGroups((current) => removeBy(current, (item) => item.groupId, groupId))
    leaveToTab('groups')
    setKickedGroup(group)
  }

  function receiveGroupInvite(invite: GroupInvite) {
    setInvites((current) => addUnique(current, invite, (item) => item.groupId))
    addToast(`${invite.from} đã mời bạn vào ${invite.name}.`)
  }

  function receiveGroupJoined(groupId: number, user: GroupMember) {
    setMembers((current) => ({ ...current, [groupId]: [...(current[groupId] ?? []), user] }))
    if (conversations[groupKey(groupId)]) appendMessage(groupKey(groupId), systemMessage(`${user.displayName} đã vào nhóm`))
  }

  function receiveGroupLeft(groupId: number, user: GroupMember) {
    removeMember(groupId, user)
  }

  function receiveGroupMessage(groupId: number, message: UserMessage) {
    const key = groupKey(groupId)
    patchConversation(key, (conversation) => ({
      unread: activeKey === key ? 0 : conversation.unread + 1,
      messages: [...conversation.messages, message],
    }))
  }

  // ---- tin offline (protocol §5.6) ----------------------------------------

  function receiveOfflineMessages(batches: OfflineBatch[], hasMore = false) {
    let received = 0
    setConversations((current) => {
      const next = { ...current }
      for (const { key, messages: incoming } of batches) {
        const conversation = next[key]
        if (!conversation) continue
        const known = new Set(conversation.messages.map((message) => message.msgId))
        const fresh = incoming.filter((message) => !known.has(message.msgId)) // khử trùng theo msg_id
        received += fresh.length
        next[key] = { ...conversation, unread: conversation.unread + fresh.length, messages: [...conversation.messages, ...fresh] }
      }
      return next
    })
    setOfflineCount((current) => current + received)
    setOfflineHasMore(hasMore)
  }

  function acknowledgeOffline(count: number) {
    if (!count) return
    const remaining = Math.max(0, offlineCount - count)
    setOfflineCount(remaining)
    if (remaining === 0) setOfflineHasMore(false)
  }

  // ---- sự kiện hệ thống --------------------------------------------------

  function receiveKicked(reason: KickReason) {
    if (reason === 'login_elsewhere') onKicked?.(reason)
    else onDisconnected?.(reason)
  }

  function pushError(code: ErrorCodeValue) {
    addToast(errorMessages[code] ?? 'Đã xảy ra lỗi.', 'error')
  }

  return {
    state: {
      tab,
      search,
      friendFilter,
      activeKey,
      activeConversation,
      infoTarget,
      conversations: conversationList,
      friends,
      pendingIn,
      pendingOut,
      onlineUsers: presenceUsers.filter((user) => user.online),
      userDirectory: presenceUsers,
      groups: groupViews,
      invites,
      activeInviteId: activeInvite?.groupId ?? null,
      members,
      inviteCandidates,
      groupFull: activeGroupMembers.length >= Limits.groupMembersMax,
      kickedGroup,
      toasts,
      offlineCount,
      offlineHasMore,
    },
    actions: {
      setTab: changeTab,
      setSearch,
      setFriendFilter,
      openConversation,
      openFriend,
      openFriendInfo,
      openGroup,
      openGroupInvite,
      acceptGroupInvite,
      declineGroupInvite,
      sendMessage,
      closeConversation,
      openInfo,
      closeInfo: () => setInfoTarget(null),
      acceptFriend,
      rejectFriend,
      receiveFriendRequest,
      receiveFriendAccepted,
      receiveFriendRejected,
      cancelFriendRequest,
      receiveFriendCancelled,
      setUserPresence,
      sendFriendRequest,
      removeFriend,
      blockFriend,
      createGroup,
      inviteToGroup,
      kickMember,
      leaveGroup,
      receiveGroupKicked,
      receiveGroupInvite,
      receiveGroupJoined,
      receiveGroupLeft,
      receiveGroupMessage,
      receiveOfflineMessages,
      acknowledgeOffline,
      receiveKicked,
      pushError,
      clearKickedNotice: () => setKickedGroup(null),
      dismissToast,
    },
  }
}

export type ChatApi = ReturnType<typeof useChatPrototype>
