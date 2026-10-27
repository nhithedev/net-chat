const timeFormat = new Intl.DateTimeFormat('vi-VN', { hour: '2-digit', minute: '2-digit' })
const dateFormat = new Intl.DateTimeFormat('vi-VN', { day: '2-digit', month: '2-digit' })

const DAY = 24 * 60 * 60 * 1000

function startOfDay(ts: number): number {
  const date = new Date(ts)
  date.setHours(0, 0, 0, 0)
  return date.getTime()
}

const fullFormat = new Intl.DateTimeFormat('vi-VN', {
  hour: '2-digit',
  minute: '2-digit',
  second: '2-digit',
  day: '2-digit',
  month: '2-digit',
  year: 'numeric',
})

const MINUTE = 60 * 1000

// "vừa xong", "5 phút trước", "3 giờ trước", "Hôm qua", hoặc ngày/tháng.
export function formatLastSeen(ts: number, now = Date.now()): string {
  const diff = now - ts
  if (diff < MINUTE) return 'vừa xong'
  if (diff < 60 * MINUTE) return `${Math.floor(diff / MINUTE)} phút trước`
  if (diff < DAY) return `${Math.floor(diff / (60 * MINUTE))} giờ trước`
  const days = Math.round((startOfDay(now) - startOfDay(ts)) / DAY)
  return days === 1 ? 'hôm qua' : dateFormat.format(ts)
}

// Dòng trạng thái hiện diện cho popup thông tin người dùng.
export function presenceLabel(user: { online: boolean; lastSeenAt?: number }): string {
  if (user.online) return 'Đang online'
  return user.lastSeenAt === undefined ? 'Ngoại tuyến' : `Online lần cuối: ${formatLastSeen(user.lastSeenAt)}`
}

const clockFormat = new Intl.DateTimeFormat('vi-VN', { hour: '2-digit', minute: '2-digit', second: '2-digit' })

// Giờ:phút:giây, dùng cho dòng log.
export function formatClock(ts: number): string {
  return clockFormat.format(ts)
}

// Đầy đủ đến giây, dùng cho tooltip khi hover tin nhắn.
export function formatFullTime(ts: number): string {
  return fullFormat.format(ts)
}

// Hôm nay -> giờ, hôm qua -> "Hôm qua", còn lại -> ngày/tháng.
export function formatTime(ts: number, now = Date.now()): string {
  const days = Math.round((startOfDay(now) - startOfDay(ts)) / DAY)
  if (days <= 0) return timeFormat.format(ts)
  if (days === 1) return 'Hôm qua'
  return dateFormat.format(ts)
}
