-- Schema PostgreSQL cho ứng dụng chat. Xem spec.md và protocol.md.
-- Thời gian dùng timestamptz; server đổi sang Unix epoch mili-giây khi gửi ra `ts`.

CREATE EXTENSION IF NOT EXISTS pg_trgm;

-- ---------------------------------------------------------------- tài khoản

CREATE TABLE users (
    id            bigserial PRIMARY KEY,
    username      text        NOT NULL UNIQUE CHECK (username ~ '^[a-z0-9_]{3,20}$'),
    display_name  text        NOT NULL CHECK (char_length(display_name) BETWEEN 1 AND 30),
    password_hash text        NOT NULL,
    salt          text        NOT NULL,
    -- mở rộng (hồ sơ)
    avatar_url    text,
    bio           text        CHECK (char_length(bio) <= 200),
    last_seen_at  timestamptz,
    created_at    timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE sessions (
    token      char(64)    PRIMARY KEY,                 -- 32 byte hex
    user_id    bigint      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at timestamptz NOT NULL,                    -- đặt now()+60s khi mất kết nối
    created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX sessions_user_idx ON sessions (user_id);

-- ------------------------------------------------------------------ bạn bè

CREATE TABLE friend_requests (
    id           bigserial PRIMARY KEY,
    requester_id bigint      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    addressee_id bigint      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    status       text        NOT NULL DEFAULT 'pending'
                 CHECK (status IN ('pending', 'accepted', 'rejected', 'cancelled')),
    created_at   timestamptz NOT NULL DEFAULT now(),
    responded_at timestamptz,
    CHECK (requester_id <> addressee_id)
);
-- chỉ một lời mời đang chờ cho mỗi cặp (không phân biệt chiều)
CREATE UNIQUE INDEX friend_requests_pending_uq
    ON friend_requests (LEAST(requester_id, addressee_id), GREATEST(requester_id, addressee_id))
    WHERE status = 'pending';
CREATE INDEX friend_requests_addressee_idx ON friend_requests (addressee_id) WHERE status = 'pending';
CREATE INDEX friend_requests_requester_idx ON friend_requests (requester_id) WHERE status = 'pending';

-- mỗi cặp bạn một dòng, user_low < user_high để không trùng chiều
CREATE TABLE friendships (
    user_low   bigint      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    user_high  bigint      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (user_low, user_high),
    CHECK (user_low < user_high)
);
CREATE INDEX friendships_high_idx ON friendships (user_high);

-- mở rộng: chặn người dùng
CREATE TABLE user_blocks (
    blocker_id bigint      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    blocked_id bigint      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (blocker_id, blocked_id),
    CHECK (blocker_id <> blocked_id)
);

-- --------------------------------------------------- hội thoại (1-1 và nhóm)

CREATE TABLE conversations (
    id         bigserial   PRIMARY KEY,                 -- với nhóm chính là group_id
    type       text        NOT NULL CHECK (type IN ('direct', 'group')),
    name       text        CHECK (char_length(name) BETWEEN 1 AND 40),
    direct_key text        UNIQUE,                      -- 'minId:maxId', chỉ cho direct
    avatar_url text,                                    -- mở rộng: ảnh nhóm
    created_at timestamptz NOT NULL DEFAULT now(),
    CHECK ((type = 'group'  AND name IS NOT NULL AND direct_key IS NULL) OR
           (type = 'direct' AND name IS NULL     AND direct_key IS NOT NULL))
);

CREATE TABLE conversation_members (
    conversation_id  bigint      NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
    user_id          bigint      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role             text        NOT NULL DEFAULT 'member' CHECK (role IN ('owner', 'member')),
    status           text        NOT NULL DEFAULT 'active' CHECK (status IN ('invited', 'active')),
    invited_by       bigint      REFERENCES users(id) ON DELETE SET NULL,
    joined_at        timestamptz,                       -- NULL khi còn invited; dùng chọn owner mới
    -- mở rộng: chưa đọc, tắt thông báo
    last_read_msg_id bigint      NOT NULL DEFAULT 0,
    muted            boolean     NOT NULL DEFAULT false,
    PRIMARY KEY (conversation_id, user_id)
);
CREATE INDEX conversation_members_user_idx ON conversation_members (user_id, status);
-- mỗi nhóm tối đa một owner
CREATE UNIQUE INDEX conversation_one_owner_uq
    ON conversation_members (conversation_id) WHERE role = 'owner';

-- ---------------------------------------------------------------- tin nhắn

CREATE TABLE messages (
    id              bigserial   PRIMARY KEY,            -- chính là msg_id trong protocol
    conversation_id bigint      NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
    sender_id       bigint      NOT NULL REFERENCES users(id),
    text            text        NOT NULL CHECK (octet_length(text) <= 4000),
    created_at      timestamptz NOT NULL DEFAULT now(),
    -- mở rộng: sửa, xóa mềm, trả lời
    edited_at       timestamptz,
    deleted_at      timestamptz,                        -- app xóa nội dung khi hiển thị
    reply_to_id     bigint      REFERENCES messages(id) ON DELETE SET NULL
);
CREATE INDEX messages_conv_idx ON messages (conversation_id, id DESC);
-- mở rộng: tìm kiếm tin
CREATE INDEX messages_text_trgm_idx ON messages USING gin (text gin_trgm_ops);

-- trạng thái giao theo từng người nhận; delivered_at NULL = tin offline chưa giao
CREATE TABLE message_receipts (
    message_id   bigint      NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
    user_id      bigint      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    delivered_at timestamptz,
    PRIMARY KEY (message_id, user_id)
);
CREATE INDEX message_receipts_pending_idx
    ON message_receipts (user_id, message_id) WHERE delivered_at IS NULL;

-- mở rộng: biểu cảm (emoji reaction)
CREATE TABLE message_reactions (
    message_id bigint      NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
    user_id    bigint      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    emoji      text        NOT NULL CHECK (char_length(emoji) BETWEEN 1 AND 16),
    created_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (message_id, user_id, emoji)
);

-- mở rộng: tệp/ảnh đính kèm (lưu metadata, nội dung ở đĩa hoặc object storage)
CREATE TABLE attachments (
    id         bigserial   PRIMARY KEY,
    message_id bigint      NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
    file_name  text        NOT NULL,
    mime_type  text        NOT NULL,
    size_bytes bigint      NOT NULL CHECK (size_bytes >= 0),
    storage_path text      NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX attachments_message_idx ON attachments (message_id);

-- ------------------------------------------------------------------- log

CREATE TABLE activity_logs (
    id      bigserial   PRIMARY KEY,
    ts      timestamptz NOT NULL DEFAULT now(),
    conn_id bigint,                                     -- do Server C cấp
    user_id bigint      REFERENCES users(id) ON DELETE SET NULL,
    event   text        NOT NULL,                       -- connect, login_ok, login_fail, disconnect, ...
    detail  jsonb                                       -- không ghi nội dung tin
);
CREATE INDEX activity_logs_ts_idx   ON activity_logs (ts DESC);
CREATE INDEX activity_logs_user_idx ON activity_logs (user_id, ts DESC);
