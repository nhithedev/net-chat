import { useState, type FormEvent } from 'react'
import LoginForm from '../components/auth/LoginForm'
import RegisterForm, { type RegisterErrors } from '../components/auth/RegisterForm'
import { useActivityLog } from '../hooks/ActivityLogContext'
import type { Account } from '../types/domain'
import { ErrorCode, Limits } from '../types/protocol'
import './AuthPage.css'

const demoAccount: Account = { username: 'demo', password: 'demo123', displayName: 'An Trần' }

type AuthPageProps = {
  registeredAccount: Account | null
  onRegistered: (account: Account) => void
  onAuthenticated: (account: Account) => void
}

function readForm(event: FormEvent<HTMLFormElement>): FormData {
  event.preventDefault()
  return new FormData(event.currentTarget)
}

const field = (data: FormData, name: string) => String(data.get(name) ?? '')

// Mock xác thực cục bộ; thay bằng REGISTER / LOGIN qua WebSocket khi nối server.
export default function AuthPage({ registeredAccount, onRegistered, onAuthenticated }: AuthPageProps) {
  const [screen, setScreen] = useState<'login' | 'register'>('login')
  const [loginError, setLoginError] = useState('')
  const [registerErrors, setRegisterErrors] = useState<RegisterErrors>({})
  const [notice, setNotice] = useState('')
  const { log } = useActivityLog()

  function showRegister() {
    setLoginError('')
    setNotice('')
    setScreen('register')
  }

  function showLogin() {
    setRegisterErrors({})
    setScreen('login')
  }

  function register(event: FormEvent<HTMLFormElement>) {
    const data = readForm(event)
    const account: Account = {
      displayName: field(data, 'displayName').trim(),
      username: field(data, 'username').trim().toLowerCase(), // server lưu chữ thường
      password: field(data, 'password'),
    }
    const errors: RegisterErrors = {}

    if (account.displayName.length < 1 || account.displayName.length > Limits.displayNameMax) {
      errors.displayName = `Tên hiển thị cần từ 1 đến ${Limits.displayNameMax} ký tự.`
    }
    if (!Limits.usernamePattern.test(account.username)) {
      errors.username = 'Dùng 3–20 ký tự gồm a-z, 0-9 hoặc dấu gạch dưới.'
    } else if (account.username === demoAccount.username || account.username === registeredAccount?.username) {
      errors.username = 'Tên đăng nhập đã tồn tại.'
    }
    if (account.password.length < Limits.passwordMin || account.password.length > Limits.passwordMax) {
      errors.password = `Mật khẩu cần từ ${Limits.passwordMin} đến ${Limits.passwordMax} ký tự.`
    }
    if (account.password !== field(data, 'confirmPassword')) errors.confirmPassword = 'Mật khẩu nhập lại chưa khớp.'

    if (Object.keys(errors).length > 0) {
      setRegisterErrors(errors)
      return
    }

    onRegistered(account)
    log('register', { username: account.username })
    setRegisterErrors({})
    setNotice('Đăng ký thành công. Hãy đăng nhập bằng tài khoản vừa tạo.')
    setScreen('login')
  }

  function login(event: FormEvent<HTMLFormElement>) {
    const data = readForm(event)
    const username = field(data, 'username').trim().toLowerCase()
    const password = field(data, 'password')
    const account = [registeredAccount, demoAccount].find((item) => item?.username === username)

    if (!account) {
      log('login_fail', { username, detail: { code: ErrorCode.UserNotFound } })
      setLoginError('Không tìm thấy tài khoản này.')
      return
    }
    if (account.password !== password) {
      log('login_fail', { username, detail: { code: ErrorCode.WrongPassword } })
      setLoginError('Mật khẩu chưa chính xác.')
      return
    }

    setLoginError('')
    log('login_ok', { username })
    onAuthenticated(account)
  }

  return (
    <main className="auth-page">
      <section className="auth-page__intro">
        <h1>
          Trò chuyện nhẹ nhàng,
          <br />
          <em>kết nối tự nhiên.</em>
        </h1>
        <p>Một không gian riêng tư để nhắn tin với bạn bè và những nhóm thân thiết.</p>
        <div className="auth-page__demo">
          <strong>Tài khoản thử nhanh</strong>
          <span>demo / demo123</span>
        </div>
      </section>
      <section className="auth-page__form">
        {screen === 'login' ? (
          <LoginForm
            error={loginError}
            notice={notice}
            defaultUsername={registeredAccount?.username}
            onSubmit={login}
            onRegister={showRegister}
          />
        ) : (
          <RegisterForm errors={registerErrors} onSubmit={register} onLogin={showLogin} />
        )}
      </section>
    </main>
  )
}
