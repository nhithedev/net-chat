import { useState, type FormEvent } from 'react'
import LoginForm from '../components/auth/LoginForm'
import RegisterForm, { type RegisterErrors } from '../components/auth/RegisterForm'
import { useActivityLog } from '../hooks/ActivityLogContext'
import { call, hasServer } from '../lib/netchat'
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

const SERVER_DOWN = 'Không kết nối được máy chủ. Hãy kiểm tra server đang chạy.'

// Trong Electron: REGISTER / LOGIN gửi tới server thật qua lõi mạng C.
// Ngoài Electron (npm run dev trên trình duyệt): xác thực giả lập cục bộ.
export default function AuthPage({ registeredAccount, onRegistered, onAuthenticated }: AuthPageProps) {
  const [screen, setScreen] = useState<'login' | 'register'>('login')
  const [loginError, setLoginError] = useState('')
  const [registerErrors, setRegisterErrors] = useState<RegisterErrors>({})
  const [registerError, setRegisterError] = useState('')
  const [notice, setNotice] = useState('')
  const [busy, setBusy] = useState(false)
  const { log } = useActivityLog()
  const live = hasServer()

  function showRegister() {
    setLoginError('')
    setRegisterError('')
    setNotice('')
    setScreen('register')
  }

  function showLogin() {
    setRegisterErrors({})
    setScreen('login')
  }

  async function register(event: FormEvent<HTMLFormElement>) {
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
    } else if (!live && (account.username === demoAccount.username || account.username === registeredAccount?.username)) {
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

    if (live) {
      setBusy(true)
      setRegisterError('')
      const res = await call('REGISTER', {
        username: account.username,
        password: account.password,
        display_name: account.displayName,
      })
      setBusy(false)
      if (!res.ok) {
        if (res.error.code === ErrorCode.AlreadyExists) setRegisterErrors({ username: 'Tên đăng nhập đã tồn tại.' })
        else if (res.error.code === ErrorCode.BadRequest) setRegisterError('Thông tin đăng ký chưa hợp lệ.')
        else setRegisterError(res.error.code < 0 ? SERVER_DOWN : res.error.message)
        return
      }
    }

    onRegistered(account)
    log('register', { username: account.username })
    setRegisterErrors({})
    setRegisterError('')
    setNotice('Đăng ký thành công. Hãy đăng nhập bằng tài khoản vừa tạo.')
    setScreen('login')
  }

  async function login(event: FormEvent<HTMLFormElement>) {
    const data = readForm(event)
    const username = field(data, 'username').trim().toLowerCase()
    const password = field(data, 'password')

    if (live) {
      setBusy(true)
      setLoginError('')
      const res = await call<{ username: string; display_name: string }>('LOGIN', { username, password })
      setBusy(false)
      if (!res.ok) {
        log('login_fail', { username, detail: { code: res.error.code } })
        if (res.error.code === ErrorCode.UserNotFound) setLoginError('Không tìm thấy tài khoản này.')
        else if (res.error.code === ErrorCode.WrongPassword) setLoginError('Mật khẩu chưa chính xác.')
        else if (res.error.code === ErrorCode.BadRequest) setLoginError('Tên đăng nhập hoặc mật khẩu chưa hợp lệ.')
        else setLoginError(res.error.code < 0 ? SERVER_DOWN : res.error.message)
        return
      }
      setNotice('')
      log('login_ok', { username: res.data.username })
      onAuthenticated({ username: res.data.username, displayName: res.data.display_name, password })
      return
    }

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
        {!live && (
          <div className="auth-page__demo">
            <strong>Tài khoản thử nhanh</strong>
            <span>demo / demo123</span>
          </div>
        )}
      </section>
      <section className="auth-page__form">
        {screen === 'login' ? (
          <LoginForm
            loading={busy}
            error={loginError}
            notice={notice}
            defaultUsername={registeredAccount?.username}
            onSubmit={login}
            onRegister={showRegister}
          />
        ) : (
          <RegisterForm loading={busy} errors={registerErrors} formError={registerError} onSubmit={register} onLogin={showLogin} />
        )}
      </section>
    </main>
  )
}
