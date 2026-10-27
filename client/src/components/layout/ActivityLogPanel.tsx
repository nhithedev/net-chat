import { useEffect, useRef } from 'react'
import { useActivityLog } from '../../hooks/ActivityLogContext'
import useDraggable from '../../hooks/useDraggable'
import { eventTone, formatDetail } from '../../lib/activity'
import { formatClock, formatFullTime } from '../../lib/format'
import Badge from '../common/Badge'
import Button from '../common/Button'
import './ActivityLogPanel.css'

const STICK_THRESHOLD_PX = 40

// Cửa sổ nổi kéo được, hiện log hệ thống (activity_logs) theo thời gian thực. Mọi người dùng đều xem được.
export default function ActivityLogPanel() {
  const { entries, panelOpen, setPanelOpen } = useActivityLog()
  const { ref: panelRef, style: panelStyle, handleProps } = useDraggable<HTMLElement>()
  const listRef = useRef<HTMLDivElement>(null)
  const stickToEnd = useRef(true)

  // Đang xem cuối danh sách thì tự cuộn theo dòng mới; cuộn lên đọc thì giữ nguyên.
  useEffect(() => {
    const list = listRef.current
    if (list && stickToEnd.current) list.scrollTop = list.scrollHeight
  }, [entries, panelOpen])

  if (!panelOpen) return null

  function onScroll() {
    const list = listRef.current
    if (list) stickToEnd.current = list.scrollHeight - list.scrollTop - list.clientHeight < STICK_THRESHOLD_PX
  }

  return (
    <aside className="log-panel" ref={panelRef} style={panelStyle} aria-label="Log hoạt động">
      <header className="log-panel__header row" {...handleProps}>
        <i className="dot" data-online="true" />
        <h2 className="grow">Log hoạt động</h2>
        <Badge label={String(entries.length)} variant="muted" />
        <Button shape="icon" size="sm" variant="outline" icon="close" ariaLabel="Tắt log hoạt động" onClick={() => setPanelOpen(false)} />
      </header>
      <div className="log-panel__list" role="log" ref={listRef} onScroll={onScroll}>
        {entries.map((entry) => (
          <div className="log-line" key={entry.id} title={formatFullTime(entry.ts)}>
            <span className="log-line__time">{formatClock(entry.ts)}</span>
            <span className="log-line__conn">{entry.connId === undefined ? '' : `#${entry.connId}`}</span>
            <span className="log-line__user truncate">{entry.username ?? '-'}</span>
            <span className="log-line__event" data-tone={eventTone[entry.event]}>{entry.event}</span>
            <span className="log-line__detail truncate">{formatDetail(entry.detail)}</span>
          </div>
        ))}
      </div>
    </aside>
  )
}
