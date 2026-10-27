import type { User } from '../../types/domain'
import Avatar from '../common/Avatar'
import Button from '../common/Button'
import Card from '../common/Card'
import ListRow from '../common/ListRow'
import PagedList from '../common/PagedList'

type BlockedListProps = {
  users: User[]
  onUnblock: (user: User) => void
}

// ext: bảng user_blocks, chưa có trong protocol.md.
export default function BlockedList({ users, onUnblock }: BlockedListProps) {
  return (
    <Card title="Đã chặn" aside={<span className="subtle">{users.length} người</span>}>
      <PagedList
        items={users}
        empty="Danh sách chặn đang trống."
        getKey={(user) => user.username}
        renderRow={(user) => (
          <ListRow
            avatar={<Avatar name={user.displayName} size="small" />}
            title={user.displayName}
            trailing={<Button variant="outline" size="sm" onClick={() => onUnblock(user)}>Bỏ chặn</Button>}
          />
        )}
      />
    </Card>
  )
}
