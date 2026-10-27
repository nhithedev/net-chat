import Button from '../common/Button'
import Dialog from '../common/Dialog'

type DeleteAccountDialogProps = {
  open: boolean
  onClose: () => void
  onConfirm: () => void
}

// ext: protocol.md chưa có lệnh xóa tài khoản.
export default function DeleteAccountDialog({ open, onClose, onConfirm }: DeleteAccountDialogProps) {
  return (
    <Dialog
      open={open}
      title="Xóa tài khoản?"
      onClose={onClose}
      actions={
        <>
          <Button variant="ghost" onClick={onClose}>Hủy</Button>
          <Button variant="danger" onClick={onConfirm}>Xóa tài khoản</Button>
        </>
      }
    >
      <p className="error">Thao tác này sẽ xóa dữ liệu tài khoản và không thể hoàn tác.</p>
    </Dialog>
  )
}
