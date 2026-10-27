import type { Friend } from '../../types/domain'
import Avatar from '../common/Avatar'
import Card from '../common/Card'
import ListRow from '../common/ListRow'

type OnlineListProps = {
  users: Friend[]
  onSelect: (user: Friend) => void
}

// F-PRE-1: mọi user đang online (ONLINE_LIST).
export default function OnlineList({ users, onSelect }: OnlineListProps) {
  return (
    <Card title="Đang online">
      <ul className="stack" data-gap="1">
        {users.map((user) => (
          <ListRow key={user.username} avatar={<Avatar name={user.displayName} online />} title={user.displayName} onClick={() => onSelect(user)} />
        ))}
      </ul>
      {users.length === 0 && <p className="subtle">Hiện chưa có ai online.</p>}
    </Card>
  )
}
