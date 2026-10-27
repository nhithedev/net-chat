import { useState } from 'react'
import usePagedSearch from '../../hooks/usePagedSearch'
import { ErrorCode } from '../../types/protocol'
import type { ActionResult, Friend } from '../../types/domain'
import Avatar from '../common/Avatar'
import Button from '../common/Button'
import Dialog from '../common/Dialog'
import ListRow from '../common/ListRow'
import Pagination from '../common/Pagination'
import TextField from '../common/TextField'

type AddFriendDialogProps = {
  open: boolean
  users: Friend[]
  loading?: boolean
  onClose: () => void
  onSubmit: (username: string) => ActionResult
}

const errorText: Record<string, string> = {
  [ErrorCode.AlreadyExists]: 'Người này đã là bạn hoặc đã có lời mời đang chờ.',
  [ErrorCode.NotFound]: 'Không tìm thấy người dùng này.',
  self: 'Bạn không thể gửi lời mời cho chính mình.',
}

// Chỉ mount khi mở nên state tự reset mỗi lần mở lại.
export default function AddFriendDialog({ open, ...props }: AddFriendDialogProps) {
  return open ? <AddFriendForm {...props} /> : null
}

function AddFriendForm({ users, loading, onClose, onSubmit }: Omit<AddFriendDialogProps, 'open'>) {
  // Ô nhập vừa là username gửi đi, vừa là từ khóa lọc danh sách gợi ý.
  const picker = usePagedSearch(users)
  const [error, setError] = useState('')
  const username = picker.query.trim()

  function submit() {
    const result = onSubmit(username)
    if (result.ok) {
      onClose()
      return
    }
    setError((result.code !== undefined && errorText[result.code]) || 'Không thể gửi lời mời lúc này.')
  }

  return (
    <Dialog
      open
      title="Thêm bạn"
      onClose={onClose}
      actions={
        <>
          <Button variant="ghost" onClick={onClose}>Hủy</Button>
          <Button disabled={loading || !username} onClick={submit}>Gửi lời mời</Button>
        </>
      }
    >
      <div className="stack">
        <TextField
          name="username"
          value={picker.query}
          placeholder="Tìm theo tên hoặc username..."
          error={error}
          onChange={(event) => picker.search(event.target.value)}
        />
        <span className="subtle">{picker.total} người dùng</span>
        <ul className="stack" data-gap="1">
          {picker.visible.map((user) => (
            <ListRow
              key={user.username}
              avatar={<Avatar name={user.displayName} size="small" online={user.online} />}
              title={user.displayName}
              subtitle={`@${user.username}`}
              onClick={() => picker.search(user.username)}
            />
          ))}
        </ul>
        {picker.visible.length === 0 && <p className="subtle">Không tìm thấy người dùng phù hợp.</p>}
        <Pagination page={picker.page} totalPages={picker.totalPages} onChange={picker.setPage} />
      </div>
    </Dialog>
  )
}
