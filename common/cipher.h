/*
 * cipher.h - mã hóa đường truyền Caesar (protocol-v2.md mục 8).
 *
 * Dịch từng BYTE của payload (không đụng header), tại chỗ, không đổi độ dài nên
 * length và giới hạn 65536 không bị ảnh hưởng; chạy đúng với UTF-8 và mọi ký tự JSON.
 * Đây chỉ là phép che dữ liệu, KHÔNG phải bảo mật. Muốn đổi thuật toán thì chỉ sửa
 * cipher.c (và tên trong HELLO.enc), không phải sửa nơi khác.
 */
#ifndef CIPHER_H
#define CIPHER_H

#include <stddef.h>
#include <stdint.h>

/* Khóa cố định, client và server cùng include; không cấu hình, không trao đổi. */
#define NETCHAT_CAESAR_KEY 3

void cipher_encode(uint8_t *p, size_t n);
void cipher_decode(uint8_t *p, size_t n);

#endif /* CIPHER_H */
