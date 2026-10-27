import { useState } from 'react'
import SessionRestoring from './components/auth/SessionRestoring'
import DisconnectedScreen from './components/feedback/DisconnectedScreen'
import KickedScreen from './components/feedback/KickedScreen'
import { addUnique, removeBy } from './lib/list'
import AuthPage from './pages/AuthPage'
import ChatPage from './pages/ChatPage'
import UserLayout from './pages/UserLayout'
import type { Account, User } from './types/domain'
import type { KickReason } from './types/protocol'

type FatalState = { kind: 'kicked' | 'disconnected'; reason: KickReason }

const RESTORE_MS = 800

export default function App() {
  const [registeredAccount, setRegisteredAccount] = useState<Account | null>(null)
  const [currentUser, setCurrentUser] = useState<Account | null>(null)
  const [screen, setScreen] = useState<'chat' | 'account'>('chat')
  const [fatalState, setFatalState] = useState<FatalState | null>(null)
  const [restoring, setRestoring] = useState(false)
  const [blockedUsers, setBlockedUsers] = useState<User[]>([])

  function logout() {
    setCurrentUser(null)
    setScreen('chat')
    setFatalState(null)
  }

  function retryConnection() {
    setFatalState(null)
    setRestoring(true)
    window.setTimeout(() => setRestoring(false), RESTORE_MS)
  }

  if (fatalState?.kind === 'kicked') return <KickedScreen onLogin={logout} />
  if (fatalState?.kind === 'disconnected') return <DisconnectedScreen reason={fatalState.reason} onRetry={retryConnection} />
  if (restoring) return <SessionRestoring />

  if (!currentUser) {
    return <AuthPage registeredAccount={registeredAccount} onRegistered={setRegisteredAccount} onAuthenticated={setCurrentUser} />
  }

  if (screen === 'account') {
    return (
      <UserLayout
        user={currentUser}
        blockedUsers={blockedUsers}
        onBack={() => setScreen('chat')}
        onUserChange={setCurrentUser}
        onUnblock={(user) => setBlockedUsers((current) => removeBy(current, (item) => item.username, user.username))}
        onLogout={logout}
        onDelete={() => {
          setRegisteredAccount(null)
          logout()
        }}
      />
    )
  }

  return (
    <ChatPage
      user={currentUser}
      onSessionExpired={logout}
      onOpenAccount={() => setScreen('account')}
      onKicked={(reason) => setFatalState({ kind: 'kicked', reason })}
      onDisconnected={(reason) => setFatalState({ kind: 'disconnected', reason })}
      onBlockUser={(user) => setBlockedUsers((current) => addUnique(current, user, (item) => item.username))}
    />
  )
}
