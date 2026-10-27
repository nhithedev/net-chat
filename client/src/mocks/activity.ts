import { ErrorCode } from '../types/protocol'
import type { ActivityLogEntry } from '../types/domain'
import { userDirectory } from './chat'

// Log mẫu của những người dùng khác trong vài phút trước. Thay bằng bảng activity_logs khi nối server.
type SeedRow = Omit<ActivityLogEntry, 'id' | 'ts'> & { secondsAgo: number }

export function seedActivityLogs(now = Date.now()): ActivityLogEntry[] {
  const [first, second, third] = userDirectory.map((user) => user.username)
  const rows: SeedRow[] = [
    { secondsAgo: 540, event: 'connect', connId: 1 },
    { secondsAgo: 538, event: 'login_ok', connId: 1, username: first },
    { secondsAgo: 420, event: 'connect', connId: 2 },
    { secondsAgo: 418, event: 'login_fail', connId: 2, username: second, detail: { code: ErrorCode.WrongPassword } },
    { secondsAgo: 405, event: 'login_ok', connId: 2, username: second },
    { secondsAgo: 300, event: 'friend_request', connId: 1, username: first, detail: { to: second } },
    { secondsAgo: 290, event: 'friend_accept', connId: 2, username: second, detail: { from: first } },
    { secondsAgo: 200, event: 'msg_send', connId: 1, username: first, detail: { to: second } },
    { secondsAgo: 150, event: 'connect', connId: 3 },
    { secondsAgo: 149, event: 'login_ok', connId: 3, username: third },
    { secondsAgo: 90, event: 'group_create', connId: 1, username: first, detail: { group_id: 1 } },
    { secondsAgo: 45, event: 'disconnect', connId: 3, username: third },
  ]
  return rows.map(({ secondsAgo, ...row }, index) => ({ id: index + 1, ts: now - secondsAgo * 1000, ...row }))
}
