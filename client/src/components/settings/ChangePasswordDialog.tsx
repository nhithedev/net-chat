import { useState } from 'react'
import type { PasswordChangeResult } from '../../types/domain'
import { Limits } from '../../types/protocol'
import Button from '../common/Button'
import Dialog from '../common/Dialog'
import TextField from '../common/TextField'

type Values = { currentPassword: string; newPassword: string; confirmPassword: string }

type ChangePasswordDialogProps = {
  open: boolean
  loading?: boolean
  onClose: () => void
  onSubmit: (values: Values) => PasswordChangeResult
}

const emptyValues: Values = { currentPassword: '', newPassword: '', confirmPassword: '' }

const errorText = {
  wrong_password: 'Mật khẩu cũ chưa chính xác.',
  weak_password: `Mật khẩu mới cần ít nhất ${Limits.passwordMin} ký tự.`,
  mismatch: 'Mật khẩu nhập lại chưa khớp.',
}

// Chỉ mount khi mở nên state tự reset mỗi lần mở lại.
export default function ChangePasswordDialog({ open, ...props }: ChangePasswordDialogProps) {
  return open ? <ChangePasswordForm {...props} /> : null
}

// F-ACC-3: CHANGE_PASSWORD cần mật khẩu cũ.
function ChangePasswordForm({ loading, onClose, onSubmit }: Omit<ChangePasswordDialogProps, 'open'>) {
  const [values, setValues] = useState<Values>(emptyValues)
  const [error, setError] = useState('')

  function bind(key: keyof Values) {
    return {
      type: 'password',
      value: values[key],
      onChange: (event: { target: { value: string } }) => setValues((current) => ({ ...current, [key]: event.target.value })),
    }
  }

  function submit() {
    const result = onSubmit(values)
    if (result.ok) onClose()
    else setError(errorText[result.code])
  }

  return (
    <Dialog
      open
      title="Đổi mật khẩu"
      onClose={onClose}
      actions={
        <>
          <Button variant="ghost" onClick={onClose}>Hủy</Button>
          <Button disabled={loading} onClick={submit}>Cập nhật</Button>
        </>
      }
    >
      <div className="stack" data-gap="4">
        <TextField label="Mật khẩu cũ" error={error} {...bind('currentPassword')} />
        <TextField label="Mật khẩu mới" {...bind('newPassword')} />
        <TextField label="Nhập lại mật khẩu" {...bind('confirmPassword')} />
      </div>
    </Dialog>
  )
}
