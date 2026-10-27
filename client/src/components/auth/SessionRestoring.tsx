import EmptyState from '../common/EmptyState'
import Spinner from '../common/Spinner'
import Screen from '../feedback/Screen'

// F-SES-3: đang RESUME bằng token đã lưu.
export default function SessionRestoring() {
  return (
    <Screen>
      <EmptyState icon={<Spinner size={34} />} title="Đang khôi phục phiên" text="Vui lòng chờ trong giây lát." />
    </Screen>
  )
}
