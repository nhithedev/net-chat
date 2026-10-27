import Badge from './Badge'

// Quan hệ với người dùng hiện tại: bạn bè hay người lạ.
export default function RelationBadge({ isFriend }: { isFriend: boolean }) {
  return <Badge label={isFriend ? 'Bạn bè' : 'Người lạ'} variant={isFriend ? 'accent' : 'muted'} />
}
