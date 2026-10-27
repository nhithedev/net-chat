import { useState } from 'react'
import Button from '../components/common/Button'
import ToastHost from '../components/common/ToastHost'
import ActivityLogToggle from '../components/layout/ActivityLogToggle'
import AccountInsightsPanel from '../components/settings/AccountInsightsPanel'
import ChangePasswordDialog from '../components/settings/ChangePasswordDialog'
import DeleteAccountDialog from '../components/settings/DeleteAccountDialog'
import LogoutDialog from '../components/settings/LogoutDialog'
import ProfilePanel from '../components/settings/ProfilePanel'
import useAccountPrototype from '../hooks/useAccountPrototype'
import type { Account, User } from '../types/domain'
import './UserLayout.css'

type UserLayoutProps = {
  user: Account
  blockedUsers: User[]
  onBack: () => void
  onUserChange: (account: Account) => void
  onUnblock: (user: User) => void
  onLogout: () => void
  onDelete: () => void
}

type DialogName = 'password' | 'logout' | 'delete'

export default function UserLayout({ user, blockedUsers, onBack, onUserChange, onUnblock, onLogout, onDelete }: UserLayoutProps) {
  const [dialog, setDialog] = useState<DialogName | null>(null)
  const { state, actions } = useAccountPrototype(user, {
    onLogout,
    onDelete,
    onProfileChange: onUserChange,
    initialBlocked: blockedUsers,
    onUnblock,
  })
  const closeDialog = () => setDialog(null)

  return (
    <main className="user-layout">
      <header className="row user-layout__header">
        <Button shape="icon" variant="outline" icon="back" ariaLabel="Quay lại màn chính" onClick={onBack} />
        <h1 className="grow">Tài khoản của tôi</h1>
        <ActivityLogToggle />
      </header>
      <div className="user-layout__grid">
        <ProfilePanel
          user={state.profile}
          onSave={actions.saveProfile}
          onPassword={() => setDialog('password')}
          onLogout={() => setDialog('logout')}
          onDelete={() => setDialog('delete')}
        />
        <AccountInsightsPanel friends={state.connections} blocked={state.blocked} onUnblock={actions.unblock} />
      </div>
      <ChangePasswordDialog open={dialog === 'password'} onClose={closeDialog} onSubmit={actions.changePassword} />
      <LogoutDialog open={dialog === 'logout'} onClose={closeDialog} onConfirm={actions.logout} />
      <DeleteAccountDialog open={dialog === 'delete'} onClose={closeDialog} onConfirm={actions.deleteAccount} />
      <ToastHost toasts={state.toasts} onDismiss={actions.dismissToast} />
    </main>
  )
}
