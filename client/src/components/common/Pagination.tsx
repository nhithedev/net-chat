import Button from './Button'

type PaginationProps = {
  page: number
  totalPages: number
  onChange: (page: number) => void
}

export default function Pagination({ page, totalPages, onChange }: PaginationProps) {
  return (
    <div className="row" data-center>
      <Button shape="icon" variant="ghost" icon="chevronLeft" ariaLabel="Trang trước" disabled={page === 1} onClick={() => onChange(page - 1)} />
      <span className="subtle">{page} / {totalPages}</span>
      <Button shape="icon" variant="ghost" icon="chevronRight" ariaLabel="Trang sau" disabled={page === totalPages} onClick={() => onChange(page + 1)} />
    </div>
  )
}
