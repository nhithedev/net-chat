import { useState } from 'react'
import type { ActionResult, User } from '../../types/domain'
import Avatar from '../common/Avatar'
import Button from '../common/Button'
import Card from '../common/Card'
import Hero from '../common/Hero'
import TextField from '../common/TextField'
import './ProfilePanel.css'

type ProfilePanelProps = {
  user: User
  onSave: (updates: { displayName: string; bio: string }) => ActionResult
  onPassword: () => void
  onLogout: () => void
  onDelete: () => void
}

// F-ACC-4: đổi tên hiển thị. bio/avatar là mở rộng (users.bio, users.avatar_url).
export default function ProfilePanel({ user, onSave, onPassword, onLogout, onDelete }: ProfilePanelProps) {
  const [editing, setEditing] = useState(false)
  const [displayName, setDisplayName] = useState(user.displayName)
  const [bio, setBio] = useState(user.bio ?? '')
  const [error, setError] = useState('')

  function startEditing() {
    setDisplayName(user.displayName)
    setBio(user.bio ?? '')
    setError('')
    setEditing(true)
  }

  function save() {
    const result = onSave({ displayName, bio })
    if (result.ok) {
      setError('')
      setEditing(false)
    } else {
      setError(result.error ?? 'Không thể lưu hồ sơ.')
    }
  }

  return (
    <Card className="profile-panel">
      {!editing && (
        <div className="profile-panel__edit">
          <Button shape="icon" variant="ghost" icon="edit" ariaLabel="Sửa hồ sơ" onClick={startEditing} />
        </div>
      )}
      <Hero avatar={<Avatar name={user.displayName} src={user.avatarUrl} size="xlarge" online />} title={user.displayName} subtitle={`@${user.username}`} />
      {editing ? (
        <div className="stack" data-gap="4">
          <TextField label="Tên hiển thị" value={displayName} error={error} onChange={(event) => setDisplayName(event.target.value)} />
          <TextField label="Bio" value={bio} multiline onChange={(event) => setBio(event.target.value)} />
          <div className="actions">
            <Button variant="ghost" onClick={() => setEditing(false)}>Hủy</Button>
            <Button onClick={save}>Lưu thay đổi</Button>
          </div>
        </div>
      ) : (
        <dl className="profile-panel__details">
          <div>
            <dt>Tên hiển thị</dt>
            <dd>{user.displayName}</dd>
          </div>
          <div>
            <dt>Tên đăng nhập</dt>
            <dd>@{user.username}</dd>
          </div>
          <div className="profile-panel__bio">
            <dt>Giới thiệu</dt>
            <dd>{user.bio || 'Chưa có phần giới thiệu.'}</dd>
          </div>
        </dl>
      )}
      <div className="profile-panel__footer card__foot">
        <div className="divider" />
        <div className="profile-panel__actions">
          <Button variant="outline" onClick={onPassword}>Đổi mật khẩu</Button>
          <Button variant="outline" onClick={onLogout}>Đăng xuất</Button>
          <Button variant="danger" onClick={onDelete}>Xóa tài khoản</Button>
        </div>
      </div>
    </Card>
  )
}
