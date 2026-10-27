import { useState } from 'react'
import { matchesQuery } from '../lib/list'
import type { Friend } from '../types/domain'
import usePagination from './usePagination'

// Lọc theo tên/username và phân trang. Dùng trong component chỉ mount khi dialog mở nên tự reset.
export default function usePagedSearch<T extends Friend>(users: T[], pageSize = 5) {
  const [query, setQuery] = useState('')
  const filtered = users.filter((user) => matchesQuery(query, user.username, user.displayName))
  const pager = usePagination(filtered, pageSize)

  function search(value: string) {
    setQuery(value)
    pager.setPage(1)
  }

  return { ...pager, query, search, total: filtered.length }
}
