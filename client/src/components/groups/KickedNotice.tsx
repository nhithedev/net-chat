import Button from '../common/Button'
import Hero from '../common/Hero'
import Overlay from '../common/Overlay'

type KickedNoticeProps = {
  groupName: string
  onClose: () => void
}

// F-UI-7: bị đuổi khỏi nhóm (EVT_GROUP_KICKED).
export default function KickedNotice({ groupName, onClose }: KickedNoticeProps) {
  return (
    <Overlay label="Bạn đã bị xóa khỏi nhóm" size="sm" onClose={onClose}>
      <Hero title="Bạn đã bị xóa khỏi nhóm" subtitle={`Bạn không còn là thành viên của ${groupName}.`}>
        <Button onClick={onClose}>Đã hiểu</Button>
      </Hero>
    </Overlay>
  )
}
