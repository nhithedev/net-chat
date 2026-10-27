import Button from '../common/Button'
import Dialog from '../common/Dialog'

type ConfirmDialogProps = {
  open: boolean
  onClose: () => void
  onConfirm: () => void
}

// F-SES-4: LOGOUT hủy token và đóng kết nối.
export default function LogoutDialog({ open, onClose, onConfirm }: ConfirmDialogProps) {
  return (
    <Dialog
      open={open}
      title="Đăng xuất?"
      onClose={onClose}
      actions={
        <>
          <Button variant="ghost" onClick={onClose}>Ở lại</Button>
          <Button onClick={onConfirm}>Đăng xuất</Button>
        </>
      }
    >
      <p className="subtle">Bạn sẽ cần đăng nhập lại để tiếp tục nhắn tin.</p>
    </Dialog>
  )
}
