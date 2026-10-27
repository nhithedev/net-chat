import type { FormEventHandler } from 'react'
import Button from '../common/Button'
import TextField from '../common/TextField'

export type RegisterErrors = Partial<Record<'displayName' | 'username' | 'password' | 'confirmPassword', string>>

type RegisterFormProps = {
  loading?: boolean
  errors?: RegisterErrors
  onSubmit: FormEventHandler<HTMLFormElement>
  onLogin: () => void
}

// F-ACC-1: REGISTER.
export default function RegisterForm({ loading, errors = {}, onSubmit, onLogin }: RegisterFormProps) {
  return (
    <form className="card stack auth-form" data-gap="4" onSubmit={onSubmit}>
      <h1>Tạo tài khoản</h1>
      <TextField label="Tên hiển thị" name="displayName" error={errors.displayName} required />
      <TextField label="Tên đăng nhập" name="username" error={errors.username} required />
      <TextField label="Mật khẩu" name="password" type="password" error={errors.password} required />
      <TextField label="Nhập lại mật khẩu" name="confirmPassword" type="password" error={errors.confirmPassword} required />
      <Button type="submit" disabled={loading}>{loading ? 'Đang tạo...' : 'Đăng ký'}</Button>
      <button className="link" type="button" onClick={onLogin}>Quay lại đăng nhập</button>
    </form>
  )
}
