import { useState } from 'react'
import { Limits } from '../../types/protocol'
import type { ActionResult } from '../../types/domain'
import Button from '../common/Button'
import Dialog from '../common/Dialog'
import TextField from '../common/TextField'

type CreateGroupDialogProps = {
  open: boolean
  loading?: boolean
  onClose: () => void
  onSubmit: (name: string) => ActionResult
}

// Chỉ mount khi mở nên state tự reset mỗi lần mở lại.
export default function CreateGroupDialog({ open, ...props }: CreateGroupDialogProps) {
  return open ? <CreateGroupForm {...props} /> : null
}

function CreateGroupForm({ loading, onClose, onSubmit }: Omit<CreateGroupDialogProps, 'open'>) {
  const [name, setName] = useState('')
  const [error, setError] = useState('')

  function submit() {
    const cleanName = name.trim()
    if (cleanName.length < 1 || cleanName.length > Limits.groupNameMax) {
      setError(`Tên nhóm cần từ 1 đến ${Limits.groupNameMax} ký tự.`)
      return
    }
    const result = onSubmit(cleanName)
    if (result.ok) onClose()
    else setError(result.error ?? 'Không thể tạo nhóm lúc này.')
  }

  return (
    <Dialog
      open
      title="Tạo nhóm"
      onClose={onClose}
      actions={
        <>
          <Button variant="ghost" onClick={onClose}>Hủy</Button>
          <Button disabled={loading || !name.trim()} onClick={submit}>Tạo nhóm</Button>
        </>
      }
    >
      <TextField label="Tên nhóm" name="name" value={name} maxLength={Limits.groupNameMax} error={error} onChange={(event) => setName(event.target.value)} />
    </Dialog>
  )
}
