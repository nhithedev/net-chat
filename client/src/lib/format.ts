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
