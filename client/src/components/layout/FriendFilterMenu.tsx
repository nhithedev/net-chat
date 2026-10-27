import type { FriendFilter } from '../../types/domain'
import Badge from '../common/Badge'
import Button from '../common/Button'

const options: Array<[FriendFilter, string]> = [
  ['all', 'Tất cả bạn bè'],
  ['requests', 'Lời mời đến'],
  ['sent', 'Lời mời đã gửi'],
  ['online', 'Đang online'],
]

type FriendFilterMenuProps = {
  active: FriendFilter
  counts: Partial<Record<FriendFilter, number>>
  onChange: (filter: FriendFilter) => void
}

export default function FriendFilterMenu({ active, counts, onChange }: FriendFilterMenuProps) {
  return (
    <div className="filter-menu">
      <Button shape="icon" variant="outline" icon="filter" ariaLabel="Lọc danh sách bạn bè" aria-haspopup="menu" />
      <div className="filter-menu__dropdown" role="menu">
        {options.map(([id, label]) => (
          <button
            className="filter-menu__option interactive row"
            type="button"
            role="menuitem"
            key={id}
            aria-current={active === id}
            onClick={() => onChange(id)}
          >
            <span className="grow">{label}</span>
            {(counts[id] ?? 0) > 0 && <Badge count={counts[id]} />}
          </button>
        ))}
      </div>
    </div>
  )
}
