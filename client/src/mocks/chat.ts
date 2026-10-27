import type { Conversation, Friend, Group, GroupInvite, GroupMember, Toast } from '../types/domain'

// Dữ liệu mẫu cho user `demo`. Thay bằng dữ liệu server khi nối protocol.

const HOUR = 60 * 60 * 1000
const DAY = 24 * HOUR

// Mốc thời gian hôm nay lúc h:m, lùi `daysAgo` ngày.
function at(hour: number, minute: number, daysAgo = 0): number {
  const date = new Date(Date.now() - daysAgo * DAY)
  date.setHours(hour, minute, 0, 0)
  return date.getTime()
}

export const demoFriends: Friend[] = [
  { username: 'thuha', displayName: 'Thu Hà', online: true, bio: 'Yêu sách, cà phê và những khu vườn yên tĩnh.' },
  { username: 'minhan', displayName: 'Minh An', online: true, bio: 'Roaster và người thích những cuộc trò chuyện dài.' },
  { username: 'binh', displayName: 'Bình Nguyễn', online: false, bio: 'Đang ngoại tuyến.' },
]

export const userDirectory: Friend[] = [
  ...demoFriends,
  { username: 'chi', displayName: 'Chi Lê', online: true, bio: 'Đọc sách và thiết kế.' },
  { username: 'linh', displayName: 'Linh Phạm', online: true, bio: 'Thành viên mới của cộng đồng.' },
  { username: 'nam', displayName: 'Nam Hoàng', online: false, bio: 'Thích nhiếp ảnh đường phố.' },
  { username: 'lan', displayName: 'Lan Vũ', online: true, bio: 'Đang học thiết kế sản phẩm.' },
  { username: 'huy', displayName: 'Huy Trần', online: false, bio: 'Yêu âm nhạc và công nghệ.' },
  { username: 'mai', displayName: 'Mai Đỗ', online: true, bio: 'Thích đọc và viết mỗi ngày.' },
]

export const demoPendingIn: Friend[] = [{ username: 'chi', displayName: 'Chi Lê', online: true }]

export const demoGroups: Group[] = [
  { groupId: 1, name: 'Study Garden', owner: 'demo' },
  { groupId: 2, name: 'Coffee Club', owner: 'thuha' },
]

export const demoMembers: Record<number, GroupMember[]> = {
  1: [{ username: 'demo', displayName: 'An Trần', online: true }, ...demoFriends],
  2: [
    { username: 'thuha', displayName: 'Thu Hà', online: true },
    { username: 'demo', displayName: 'An Trần', online: true },
    { username: 'minhan', displayName: 'Minh An', online: true },
  ],
}

export const demoInvites: GroupInvite[] = [
  { groupId: 3, name: 'Weekend Reading', from: 'Thu Hà', fromUsername: 'thuha' },
]

export const demoToasts: Toast[] = [
  { id: 'friend-request-initial', kind: 'info', text: 'Chi Lê đã gửi lời mời kết bạn.' },
  { id: 'group-invite-initial', kind: 'info', text: 'Thu Hà đã mời bạn vào Weekend Reading.' },
]

export const demoConversations: Record<string, Conversation> = {
  'dm:thuha': {
    key: 'dm:thuha',
    kind: 'dm',
    title: 'Thu Hà',
    username: 'thuha',
    online: true,
    peerStatus: 'ok',
    unread: 2,
    messages: [
      { msgId: 1, from: 'thuha', displayName: 'Thu Hà', text: 'Chiều nay bạn có ghé hiệu sách không?', ts: at(10, 36), delivered: true },
      { msgId: 2, from: 'demo', text: 'Có, mình sẽ đến sau giờ làm.', ts: at(10, 38), delivered: true },
      { msgId: 3, from: 'thuha', displayName: 'Thu Hà', text: 'Mình giữ chỗ cạnh cửa sổ nhé.', ts: at(10, 41), delivered: true },
    ],
  },
  'dm:minhan': {
    key: 'dm:minhan',
    kind: 'dm',
    title: 'Minh An',
    username: 'minhan',
    online: true,
    peerStatus: 'ok',
    unread: 0,
    messages: [
      { msgId: 4, from: 'minhan', displayName: 'Minh An', text: 'Mẻ cà phê mới đã sẵn sàng.', ts: at(9, 6), delivered: true },
    ],
  },
  'dm:binh': {
    key: 'dm:binh',
    kind: 'dm',
    title: 'Bình Nguyễn',
    username: 'binh',
    online: false,
    peerStatus: 'disconnected',
    unread: 0,
    messages: [
      { msgId: 5, from: 'binh', displayName: 'Bình Nguyễn', text: 'Mình sẽ liên hệ lại sau nhé.', ts: at(20, 15, 1), delivered: true },
    ],
  },
  'group:1': {
    key: 'group:1',
    kind: 'group',
    groupId: 1,
    title: 'Study Garden',
    peerStatus: 'ok',
    unread: 2,
    messages: [
      { msgId: 'system-1', system: true, text: 'Bình Nguyễn đã vào nhóm' },
      { msgId: 6, from: 'thuha', displayName: 'Thu Hà', text: 'Mọi người đọc tới chương ba chưa?', ts: at(8, 24), delivered: true },
      { msgId: 7, from: 'demo', text: 'Mình vừa ghi chú xong phần cuối.', ts: at(8, 30), delivered: true },
    ],
  },
  'group:2': {
    key: 'group:2',
    kind: 'group',
    groupId: 2,
    title: 'Coffee Club',
    peerStatus: 'ok',
    unread: 0,
    messages: [
      { msgId: 'system-2', system: true, text: 'An Trần đã vào nhóm' },
      { msgId: 8, from: 'minhan', displayName: 'Minh An', text: 'Cuối tuần này mình thử hạt mới nhé.', ts: at(9, 12), delivered: true },
    ],
  },
}

export const demoOfflineCount = 137
