import { useState } from 'react'
import { removeBy } from '../lib/list'
import { Limits } from '../types/protocol'
import type { Account, ActionResult, Friend, PasswordChangeResult, User } from '../types/domain'
import useToasts from './useToasts'

type Options = {
  initialBlocked?: User[]
  onLogout?: () => void
  onDelete?: () => void
  onProfileChange?: (account: Account) => void
  onUnblock?: (user: User) => void
}

type ProfileUpdates = { displayName: string; bio: string }
type PasswordForm = { currentPassword: string; newPassword: string; confirmPassword: string }

const connections: Friend[] = [
  { username: 'thuha', displayName: 'Thu Hà', online: true },
  { username: 'minhan', displayName: 'Minh An', online: true },
  { username: 'binh', displayName: 'Bình Nguyễn', online: false },
]

export default function useAccountPrototype(
  initialUser: Account,
  { initialBlocked = [], onLogout, onDelete, onProfileChange, onUnblock }: Options = {},
) {
  const [profile, setProfile] = useState<Account>({
    ...initialUser,
    bio: initialUser.bio || 'Thích những cuộc trò chuyện yên tĩnh và chân thành.',
  })
  const [blocked, setBlocked] = useState<User[]>(initialBlocked)
  const { toasts, addToast, dismissToast } = useToasts()

  function saveProfile(updates: ProfileUpdates): ActionResult {
    const displayName = updates.displayName.trim()
    if (displayName.length < 1 || displayName.length > Limits.displayNameMax) {
      return { ok: false, error: `Tên hiển thị cần từ 1 đến ${Limits.displayNameMax} ký tự.` }
    }
    const next = { ...profile, ...updates, displayName }
    setProfile(next)
    onProfileChange?.(next)
    addToast('Đã lưu thay đổi hồ sơ.')
    return { ok: true }
  }

  function changePassword({ currentPassword, newPassword, confirmPassword }: PasswordForm): PasswordChangeResult {
    if (currentPassword !== profile.password) return { ok: false, code: 'wrong_password' }
    if (newPassword.length < Limits.passwordMin) return { ok: false, code: 'weak_password' }
    if (newPassword !== confirmPassword) return { ok: false, code: 'mismatch' }
    setProfile((current) => ({ ...current, password: newPassword }))
    addToast('Đã đổi mật khẩu.')
    return { ok: true }
  }

  function unblock(user: User) {
    setBlocked((current) => removeBy(current, (item) => item.username, user.username))
    onUnblock?.(user)
    addToast(`Đã bỏ chặn ${user.displayName}.`)
  }

  return {
    state: { profile, blocked, connections, toasts },
    actions: {
      saveProfile,
      changePassword,
      unblock,
      logout: () => onLogout?.(),
      deleteAccount: () => onDelete?.(),
      dismissToast,
    },
  }
}
