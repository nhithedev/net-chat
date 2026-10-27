import defaultAvatar from '../../../assets/avt.jpg'
import './Avatar.css'

type AvatarProps = {
  name?: string
  src?: string // users.avatar_url; thiếu thì dùng ảnh mặc định
  size?: 'small' | 'medium' | 'large' | 'xlarge'
  online?: boolean
}

export default function Avatar({ name, src, size = 'medium', online }: AvatarProps) {
  return (
    <span className="avatar" data-size={size}>
      <img src={src || defaultAvatar} alt={name ?? ''} />
      {online !== undefined && <i className="dot avatar__status" data-online={online} />}
    </span>
  )
}
