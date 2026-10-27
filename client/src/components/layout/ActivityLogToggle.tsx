import { useActivityLog } from '../../hooks/ActivityLogContext'
import Button from '../common/Button'

// Bật/tắt panel log hoạt động. Trạng thái nằm ở store nên giữ nguyên khi chuyển trang.
export default function ActivityLogToggle() {
  const { panelOpen, setPanelOpen } = useActivityLog()
  return (
    <Button
      variant={panelOpen ? 'soft' : 'outline'}
      icon="terminal"
      aria-pressed={panelOpen}
      onClick={() => setPanelOpen(!panelOpen)}
    >
      {panelOpen ? 'Tắt log hoạt động' : 'Bật log hoạt động'}
    </Button>
  )
}
