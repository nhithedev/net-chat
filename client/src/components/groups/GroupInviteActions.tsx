import Button from '../common/Button'
import './GroupInviteActions.css'

type GroupInviteActionsProps = {
  groupName: string
  onAccept: () => void
  onDecline: () => void
}

export default function GroupInviteActions({ groupName, onAccept, onDecline }: GroupInviteActionsProps) {
  return (
    <div className="invite-actions row" data-gap="4">
      <p className="subtle grow">Bạn cần phản hồi lời mời để tham gia cuộc trò chuyện trong {groupName}.</p>
      <div className="actions">
        <Button variant="outline" onClick={onDecline}>Từ chối</Button>
        <Button onClick={onAccept}>Chấp nhận</Button>
      </div>
    </div>
  )
}
