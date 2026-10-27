// Mã lỗi theo protocol.md §6.
export const ErrorCode = {
  BadRequest: 1,
  NotAuthenticated: 2,
  Permission: 3,
  NotFriends: 4,
  NotMember: 5,
  AlreadyExists: 6,
  NotFound: 7,
  TooLarge: 8,
  RateLimit: 9,
  Protocol: 10,
  Internal: 11,
  UserNotFound: 1001,
  WrongPassword: 1002,
  TokenInvalid: 1003,
} as const

export type ErrorCodeValue = (typeof ErrorCode)[keyof typeof ErrorCode]

// Giới hạn theo protocol.md §7 / spec.md §4.
export const Limits = {
  usernamePattern: /^[a-z0-9_]{3,20}$/,
  displayNameMax: 30,
  passwordMin: 6,
  passwordMax: 64,
  groupNameMax: 40,
  groupMembersMax: 100,
} as const

export type KickReason = 'login_elsewhere' | 'timeout' | 'server_shutdown' | 'protocol_error'
