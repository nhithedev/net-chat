import Button from '../common/Button'
import Screen from './Screen'

// F-SES-5: EVT_KICKED (login_elsewhere).
export default function KickedScreen({ onLogin }: { onLogin: () => void }) {
  return (
    <Screen title="Đã đăng nhập ở nơi khác" text="Phiên hiện tại đã kết thúc để bảo vệ tài khoản của bạn.">
      <Button onClick={onLogin}>Đăng nhập lại</Button>
    </Screen>
  )
}
