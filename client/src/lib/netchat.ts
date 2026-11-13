// Giao diện `window.netchat` do preload.cjs phơi ra (protocol-v2.md mục 10.4).
// Chỉ có trong Electron; chạy bằng trình duyệt thường (npm run dev) thì không có
// và giao diện dùng dữ liệu giả.

export type NetchatError = { code: number; message: string }
export type NetchatResult<T = Record<string, unknown>> =
  | { ok: true; data: T }
  | { ok: false; error: NetchatError }

export interface NetchatApi {
  connect(opts?: { host?: string; port?: number }): Promise<{ ok: true } | { ok: false; error: NetchatError }>
  disconnect(): Promise<void>
  request<T = Record<string, unknown>>(type: string, data?: object): Promise<NetchatResult<T>>
  onEvent(cb: (evt: { type: string; data: unknown }) => void): () => void
  onStatus(cb: (s: { state: 'connecting' | 'open' | 'reconnecting' | 'restored' | 'closed'; reason?: string }) => void): () => void
}

declare global {
  interface Window {
    netchat?: NetchatApi
  }
}

/** Có đang chạy trong Electron với lõi mạng không? */
export function hasServer(): boolean {
  return typeof window !== 'undefined' && window.netchat !== undefined
}

/**
 * Gửi một request lên server thật. Tự kết nối lại nếu kết nối đã đóng (server đóng
 * kết nối chưa đăng nhập sau 30 giây, hoặc sau khi đăng xuất).
 */
export async function call<T = Record<string, unknown>>(type: string, data?: object): Promise<NetchatResult<T>> {
  const api = window.netchat
  if (!api) return { ok: false, error: { code: -1, message: 'Không có lõi mạng' } }
  const conn = await api.connect()
  if (!conn.ok) return { ok: false, error: conn.error }
  return api.request<T>(type, data)
}
