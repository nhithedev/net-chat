import type { GroupInvite } from '../../types/domain'
import Avatar from '../common/Avatar'
import ListRow from '../common/ListRow'

type GroupInviteCardProps = {
  invite: GroupInvite
  selected?: boolean
  onSelect: (invite: GroupInvite) => void
}

export default function GroupInviteCard({ invite, selected = false, onSelect }: GroupInviteCardProps) {
  return (
    <ListRow
      avatar={<Avatar name={invite.name} />}
      title={invite.name}
      subtitle={`${invite.from} đã mời bạn`}
      selected={selected}
      onClick={() => onSelect(invite)}
    />
  )
}
