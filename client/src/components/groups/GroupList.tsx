import type { GroupInvite, GroupView } from '../../types/domain'
import Avatar from '../common/Avatar'
import Button from '../common/Button'
import EmptyState from '../common/EmptyState'
import ListRow from '../common/ListRow'
import GroupInviteCard from './GroupInviteCard'
import './GroupList.css'

type GroupListProps = {
  groups: GroupView[]
  invites: GroupInvite[]
  activeInviteId: number | null
  onSelect: (group: GroupView) => void
  onSelectInvite: (invite: GroupInvite) => void
}

export default function GroupList({ groups, invites, activeInviteId, onSelect, onSelectInvite }: GroupListProps) {
  return (
    <div className="stack" data-gap="1">
      {invites.length > 0 && (
        <>
          <small className="group-list__label">Lời mời nhóm</small>
          <ul className="stack" data-gap="1">
            {invites.map((invite) => (
              <GroupInviteCard
                key={invite.groupId}
                invite={invite}
                selected={invite.groupId === activeInviteId}
                onSelect={onSelectInvite}
              />
            ))}
          </ul>
        </>
      )}
      {groups.length > 0 && <small className="group-list__label">Đã tham gia</small>}
      <ul className="stack" data-gap="1">
        {groups.map((group) => (
          <ListRow
            key={group.groupId}
            avatar={<Avatar name={group.name} />}
            title={group.name}
            subtitle={`${group.memberCount} thành viên`}
            onClick={() => onSelect(group)}
            trailing={<Button shape="icon" variant="ghost" icon="message" ariaLabel={`Mở chat ${group.name}`} onClick={() => onSelect(group)} />}
          />
        ))}
      </ul>
      {groups.length === 0 && <EmptyState compact icon={null} text="Bạn chưa tham gia nhóm nào." />}
    </div>
  )
}
