import { useEffect, useState } from 'react'
import Banner from '../common/Banner'

type OfflineNoticeProps = {
  count: number
  hasMore?: boolean
}

const VISIBLE_MS = 4000

// F-OFF-2: báo số tin nhận được khi offline, tự ẩn sau vài giây.
// Mount lại (đổi `key`) mỗi khi count/hasMore đổi để hiện lại.
export default function OfflineNotice({ count, hasMore = false }: OfflineNoticeProps) {
  const [visible, setVisible] = useState(true)

  useEffect(() => {
    const timer = window.setTimeout(() => setVisible(false), VISIBLE_MS)
    return () => window.clearTimeout(timer)
  }, [])

  if (!count || !visible) return null
  return (
    <Banner>
      <span>
        Bạn có <strong>{count}</strong> tin nhắn mới khi offline.
        {hasMore && ' Đang kết nối...'}
      </span>
    </Banner>
  )
}
