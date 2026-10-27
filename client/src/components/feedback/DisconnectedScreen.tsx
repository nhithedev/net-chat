import type { KickReason } from '../../types/protocol'
import Button from '../common/Button'
import Screen from './Screen'

type DisconnectedScreenProps = {
  reason?: KickReason
  loading?: boolean
  onRetry: () => void
}

export default function DisconnectedScreen({ reason, loading, onRetry }: DisconnectedScreenProps) {
  const text = reason === 'server_shutdown' ? 'Máy chủ đang tạm dừng.' : 'Không thể kết nối tới máy chủ.'
  return (
    <Screen title="Mất kết nối" text={text}>
      <Button disabled={loading} onClick={onRetry}>{loading ? 'Đang thử lại...' : 'Thử lại'}</Button>
    </Screen>
  )
}
