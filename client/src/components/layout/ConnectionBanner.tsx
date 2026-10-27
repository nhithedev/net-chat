import type { ConnectionStatus } from '../../types/domain'
import Banner from '../common/Banner'

const labels: Record<Exclude<ConnectionStatus, 'open'>, string> = {
  connecting: 'Đang kết nối...',
  reconnecting: 'Đang khôi phục phiên...',
  closed: 'Mất kết nối. Tin nhắn mới sẽ được lưu để gửi lại.',
  restored: 'Đã kết nối lại',
}

const tones = {
  connecting: 'warning',
  reconnecting: 'warning',
  closed: 'danger',
  restored: 'info',
} as const

// F-UI-6: trạng thái kết nối.
export default function ConnectionBanner({ status }: { status: ConnectionStatus }) {
  if (status === 'open') return null
  return (
    <Banner tone={tones[status]}>
      <i className="dot" />
      {labels[status]}
    </Banner>
  )
}
