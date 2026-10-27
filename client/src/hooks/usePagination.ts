import { useState } from 'react'

// Phân trang danh sách; tự kẹp trang khi danh sách ngắn lại (vd. xóa phần tử cuối của trang).
export default function usePagination<T>(items: T[], pageSize = 5) {
  const [requested, setPage] = useState(1)
  const totalPages = Math.max(1, Math.ceil(items.length / pageSize))
  const page = Math.min(requested, totalPages)
  const visible = items.slice((page - 1) * pageSize, page * pageSize)
  return { page, setPage, totalPages, visible }
}
