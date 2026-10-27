import type { Friend } from '../../types/domain'
import Avatar from '../common/Avatar'
import Card from '../common/Card'
import ListRow from '../common/ListRow'
import PagedList from '../common/PagedList'
import RelationBadge from '../common/RelationBadge'

type OnlineListProps = {
  users: Friend[]
  isFriend: (username: string) => boolean
  onSelect: (user: Friend) => void
}

const PAGE_SIZE = 8

// F-PRE-1: mọi user đang online trong hệ thống (ONLINE_LIST), không riêng bạn bè.
export default function OnlineList({ users, isFriend, onSelect }: OnlineListProps) {
  return (
    <Card title="Đang online" aside={<span className="subtle">{users.length} người</span>}>
      <PagedList
        items={users}
        pageSize={PAGE_SIZE}
        empty="Hiện chưa có ai online."
        getKey={(user) => user.username}
        renderRow={(user) => (
          <ListRow
            avatar={<Avatar name={user.displayName} online />}
            title={user.displayName}
            subtitle={`@${user.username}`}
            trailing={<RelationBadge isFriend={isFriend(user.username)} />}
            onClick={() => onSelect(user)}
          />
        )}
      />
    </Card>
  )
}
