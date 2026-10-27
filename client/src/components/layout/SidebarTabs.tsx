import type { Tab } from '../../types/domain'
import Badge from '../common/Badge'

const tabs: Array<[Tab, string]> = [
  ['messages', 'Tin nhắn'],
  ['friends', 'Bạn bè'],
  ['groups', 'Nhóm'],
]

type SidebarTabsProps = {
  active: Tab
  unread: Record<Tab, number>
  onChange: (tab: Tab) => void
}

export default function SidebarTabs({ active, unread, onChange }: SidebarTabsProps) {
  return (
    <nav className="sidebar-tabs">
      {tabs.map(([id, label]) => (
        <button
          className="sidebar-tabs__item interactive row"
          type="button"
          key={id}
          aria-current={active === id}
          onClick={() => onChange(id)}
        >
          <span>{label}</span>
          {unread[id] > 0 && <Badge count={unread[id]} />}
        </button>
      ))}
    </nav>
  )
}
