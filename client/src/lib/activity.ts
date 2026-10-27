import type { ActivityEvent, LogDetail } from '../types/domain'
import type { KickReason } from '../types/protocol'

export type LogTone = 'neutral' | 'info' | 'warning' | 'danger'

// Màu thẻ sự kiện trong panel log (dùng chung bộ [data-tone] ở ui.css).
export const eventTone: Record<ActivityEvent, LogTone> = {
  connect: 'info',
  register: 'info',
  login_ok: 'info',
  login_fail: 'danger',
  logout: 'warning',
  disconnect: 'warning',
  timeout: 'danger',
  protocol_error: 'danger',
  friend_request: 'neutral',
  friend_accept: 'neutral',
  friend_reject: 'warning',
  friend_cancel: 'warning',
  friend_remove: 'warning',
  group_create: 'neutral',
  group_invite: 'neutral',
  group_join: 'neutral',
  group_leave: 'warning',
  group_kick: 'danger',
  msg_send: 'neutral',
  group_msg_send: 'neutral',
}

// EVT_KICKED.reason -> sự kiện ghi log.
export function eventForKick(reason: KickReason): ActivityEvent {
  return reason === 'timeout' || reason === 'protocol_error' ? reason : 'disconnect'
}

export function formatDetail(detail: LogDetail | undefined): string {
  return detail ? Object.entries(detail).map(([key, value]) => `${key}=${value}`).join(' ') : ''
}
