import type { Friend, User } from '../../types/domain'
import Avatar from '../common/Avatar'
import Card from '../common/Card'
import ListRow from '../common/ListRow'
import PagedList from '../common/PagedList'
import BlockedList from './BlockedList'
import './AccountInsightsPanel.css'

type AccountInsightsPanelProps = {
  friends: Friend[]
  blocked: User[]
  onUnblock: (user: User) => void
}

// Hai card chia đều chiều cao cột phải.
export default function AccountInsightsPanel({ friends, blocked, onUnblock }: AccountInsightsPanelProps) {
  return (
    <div className="insights">
      <Card title="Bạn bè" aside={<span className="subtle">{friends.length} người</span>}>
        <PagedList
          items={friends}
          empty="Danh sách bạn bè đang trống."
          getKey={(friend) => friend.username}
          renderRow={(friend) => (
            <ListRow avatar={<Avatar name={friend.displayName} size="small" online={friend.online} />} title={friend.displayName} />
          )}
        />
      </Card>
      <BlockedList users={blocked} onUnblock={onUnblock} />
    </div>
  )
}
