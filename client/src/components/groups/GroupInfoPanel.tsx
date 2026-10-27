import type { Group, GroupMember } from '../../types/domain'
import Avatar from '../common/Avatar'
import Badge from '../common/Badge'
import Button from '../common/Button'
import Hero from '../common/Hero'
import ListRow from '../common/ListRow'
import RelationBadge from '../common/RelationBadge'

type GroupInfoPanelProps = {
  group: Group
  members: GroupMember[]
  currentUser: string
  isFriend: (username: string) => boolean
  onInvite: () => void
  onKick: (member: GroupMember) => void
  onLeave: () => void
}

// F-GRP-3/4/5: xem thành viên, mời thêm, owner đuổi, rời nhóm.
export default function GroupInfoPanel({ group, members, currentUser, isFriend, onInvite, onKick, onLeave }: GroupInfoPanelProps) {
  const isOwner = currentUser === group.owner
  return (
    <div className="stack">
      <Hero avatar={<Avatar name={group.name} size="large" />} title={group.name} subtitle={`${members.length} thành viên`} />
      <div className="divider" />
      <div className="row">
        <h3 className="grow">Thành viên</h3>
        <Button variant="soft" onClick={onInvite}>Mời thêm</Button>
      </div>
      <ul className="stack" data-gap="1">
        {members.map((member) => (
          <ListRow
            key={member.username}
            avatar={<Avatar name={member.displayName} size="small" online={member.online} />}
            title={member.displayName}
            trailing={
              <>
                {member.username === group.owner && <Badge label="Owner" variant="muted" />}
                {member.username !== currentUser && <RelationBadge isFriend={isFriend(member.username)} />}
                {isOwner && member.username !== currentUser && (
                  <button className="link error" type="button" onClick={() => onKick(member)}>Đuổi</button>
                )}
              </>
            }
          />
        ))}
      </ul>
      <Button variant="outline" onClick={onLeave}>Rời nhóm</Button>
    </div>
  )
}
