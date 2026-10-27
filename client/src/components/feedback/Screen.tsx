import type { ReactNode } from 'react'
import './Screen.css'

type ScreenProps = {
  title?: string
  text?: string
  children?: ReactNode
}

// Toàn màn hình, căn giữa. Có `title` thì hiện thêm thẻ thông báo.
export default function Screen({ title, text, children }: ScreenProps) {
  return (
    <main className="screen">
      {title ? (
        <section className="card stack screen__card">
          <h1>{title}</h1>
          {text && <p className="subtle">{text}</p>}
          {children}
        </section>
      ) : (
        children
      )}
    </main>
  )
}
