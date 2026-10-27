import type { FormEventHandler } from 'react'
import Banner from '../common/Banner'
import Button from '../common/Button'
import TextField from '../common/TextField'

type LoginFormProps = {
  loading?: boolean
  error?: string
  notice?: string
  defaultUsername?: string
  onSubmit: FormEventHandler<HTMLFormElement>
  onRegister: () => void
}

export default function LoginForm({ loading, error, notice, defaultUsername, onSubmit, onRegister }: LoginFormProps) {
  return (
    <form className="card stack auth-form" data-gap="4" onSubmit={onSubmit}>
      <div className="hero">
        <h1>Chào mừng trở lại</h1>
        <p className="subtle">Đăng nhập để tiếp tục cuộc trò chuyện.</p>
      </div>
      <TextField label="Tên đăng nhập" name="username" autoComplete="username" defaultValue={defaultUsername} required />
      <TextField label="Mật khẩu" name="password" type="password" autoComplete="current-password" required />
      {notice && <Banner inline>{notice}</Banner>}
      {error && <p className="error">{error}</p>}
      <Button type="submit" disabled={loading}>{loading ? 'Đang đăng nhập...' : 'Đăng nhập'}</Button>
      <p className="subtle auth-form__foot">
        Chưa có tài khoản? <button className="link" type="button" onClick={onRegister}>Đăng ký</button>
      </p>
    </form>
  )
}
