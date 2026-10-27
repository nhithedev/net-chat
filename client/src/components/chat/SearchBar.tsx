import type { ChangeEventHandler } from 'react'
import Button from '../common/Button'
import TextField from '../common/TextField'
import './SearchBar.css'

type SearchBarProps = {
  value: string
  resultCount?: number
  onChange: ChangeEventHandler<HTMLInputElement | HTMLTextAreaElement>
  onClose: () => void
}

// P2: tìm kiếm tin trong hội thoại (schema.sql: messages_text_trgm_idx).
export default function SearchBar({ value, resultCount = 0, onChange, onClose }: SearchBarProps) {
  return (
    <div className="search-bar row">
      <div className="grow">
        <TextField aria-label="Tìm trong cuộc trò chuyện" value={value} placeholder="Tìm trong cuộc trò chuyện..." onChange={onChange} />
      </div>
      <span className="subtle">{resultCount} kết quả</span>
      <Button shape="icon" size="sm" variant="outline" icon="close" ariaLabel="Đóng tìm kiếm" onClick={onClose} />
    </div>
  )
}
