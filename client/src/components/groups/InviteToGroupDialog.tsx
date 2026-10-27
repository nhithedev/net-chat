import { useState } from 'react'
import usePagedSearch from '../../hooks/usePagedSearch'
import type { ActionResult, Friend } from '../../types/domain'
import Avatar from '../common/Avatar'
import Button from '../common/Button'
import Dialog from '../common/Dialog'
import ListRow from '../common/ListRow'
import Pagination from '../common/Pagination'
import RelationBadge from '../common/RelationBadge'
import TextField from '../common/TextField'

type Candidate = Friend & { isFriend: boolean }

type InviteToGroupDialogProps = {
  open: boolean
  users: Candidate[]
  full: boolean
  onClose: () => void
  onSubmit: (usernames: string[]) => ActionResult
}

// Chỉ mount khi mở nên state tự reset mỗi lần mở lại.
export default function InviteToGroupDialog({ open, ...props }: InviteToGroupDialogProps) {
  return open ? <InviteForm {...props} /> : null
}

// F-GRP-2: chỉ được mời bạn bè (ERR_NOT_FRIENDS); người lạ vẫn hiện để server trả lỗi.
function InviteForm({ users, full, onClose, onSubmit }: Omit<InviteToGroupDialogProps, 'open'>) {
  const picker = usePagedSearch(users)
  const [selected, setSelected] = useState<string[]>([])

  function toggle(username: string) {
    setSelected((current) => (current.includes(username) ? current.filter((item) => item !== username) : [...current, username]))
  }

  function submit() {
    if (onSubmit(selected).ok) onClose()
  }

  return (
    <Dialog
      open
      title="Mời vào nhóm"
      onClose={onClose}
      actions={
        <>
          <Button variant="ghost" onClick={onClose}>Hủy</Button>
          <Button disabled={full || selected.length === 0} onClick={submit}>Gửi lời mời</Button>
        </>
      }
    >
      <div className="stack">
        {full && <p className="error">Nhóm đã đủ 100 thành viên.</p>}
        <TextField value={picker.query} placeholder="Tìm người dùng..." aria-label="Tìm người dùng" onChange={(event) => picker.search(event.target.value)} />
        <span className="subtle">{picker.total} người dùng</span>
        <div className="stack" data-gap="1">
          {picker.visible.map((user) => (
            <ListRow
              as="label"
              key={user.username}
              leading={<input type="checkbox" checked={selected.includes(user.username)} onChange={() => toggle(user.username)} />}
              avatar={<Avatar name={user.displayName} size="small" />}
              title={user.displayName}
              subtitle={`@${user.username}`}
              trailing={<RelationBadge isFriend={user.isFriend} />}
            />
          ))}
        </div>
        {picker.visible.length === 0 && !full && <p className="subtle">Không còn người dùng phù hợp để mời.</p>}
        <Pagination page={picker.page} totalPages={picker.totalPages} onChange={picker.setPage} />
      </div>
    </Dialog>
  )
}
