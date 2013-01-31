#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#define PKT_TYPE_RR 0xAA  // Receive ready
#define PKT_TYPE_REJ 0x55 // Reject
#define PKT_TYPE_BUF 0x99 // Buffer size
#define PKT_TYPE_FLN 0x33 // Filename
#define PKT_TYPE_DAT 0xBB // Data
#define PKT_TYPE_WIN 0xCC // Window size

#define PKT_DMAX 1400
#define PKT_TRNSMAX 10

#pragma pack(push, 1)
struct packet {
    uint8_t type;
    uint32_t sequence;
    uint16_t checksum;
    uint16_t size;
    uint8_t data[PKT_DMAX];
};
#pragma pack(pop)

/* returns a retransmission packet */
struct packet retransmitpkt(uint32_t sequence);

/* returns an acknowledgment packet */
struct packet ackpkt(const struct packet *src);

#endif
