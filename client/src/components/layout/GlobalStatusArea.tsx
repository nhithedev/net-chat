import type { ConnectionStatus } from '../../types/domain'
import OfflineNotice from '../chat/OfflineNotice'
import ConnectionBanner from './ConnectionBanner'

type GlobalStatusAreaProps = {
  connectionStatus: ConnectionStatus
  offlineCount: number
  offlineHasMore: boolean
}

export default function GlobalStatusArea({ connectionStatus, offlineCount, offlineHasMore }: GlobalStatusAreaProps) {
  if (connectionStatus === 'open' && offlineCount === 0) return null
  return (
    <div className="global-status">
      <ConnectionBanner status={connectionStatus} />
      <OfflineNotice key={`${offlineCount}-${offlineHasMore}`} count={offlineCount} hasMore={offlineHasMore} />
    </div>
  )
}
