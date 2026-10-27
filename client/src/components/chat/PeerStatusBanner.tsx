import type { PeerStatus } from '../../types/domain'
import Banner from '../common/Banner'

type PeerStatusBannerProps = {
  name: string
  status?: PeerStatus
}

// F-DM-5: bên kia rớt mạng / ngắt kết nối.
export default function PeerStatusBanner({ name, status }: PeerStatusBannerProps) {
  if (!status || status === 'ok') return null
  return <Banner tone="warning">{name} đã ngắt kết nối</Banner>
}
