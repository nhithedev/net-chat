import { Fragment, type ReactNode } from 'react'
import usePagination from '../../hooks/usePagination'
import Pagination from './Pagination'

type PagedListProps<T> = {
  items: T[]
  empty: string
  pageSize?: number
  getKey: (item: T) => string
  renderRow: (item: T) => ReactNode
}

// Dùng trong <Card>: danh sách có phân trang (chỉ hiện khi quá một trang) hoặc chữ "trống" ở giữa.
export default function PagedList<T>({ items, empty, pageSize = 5, getKey, renderRow }: PagedListProps<T>) {
  const pager = usePagination(items, pageSize)

  if (items.length === 0) return <p className="subtle card__empty">{empty}</p>

  return (
    <>
      <ul className="stack" data-gap="1">
        {pager.visible.map((item) => (
          <Fragment key={getKey(item)}>{renderRow(item)}</Fragment>
        ))}
      </ul>
      {pager.totalPages > 1 && (
        <div className="card__foot">
          <Pagination page={pager.page} totalPages={pager.totalPages} onChange={pager.setPage} />
        </div>
      )}
    </>
  )
}
