#include <arpa/inet.h>
#include <string.h>

#include "packet.h"
#include "select_call.h"
#include "cpe464.h"

const char *pkttypestr(uint8_t type) {
    switch (type) {
        case PKT_TYPE_CXN:
            return "Connection Request";
        case PKT_TYPE_RR:
            return "Receive Ready";
        case PKT_TYPE_REJ:
            return "Reject";
        case PKT_TYPE_BUF:
            return "Buffer Size";
        case PKT_TYPE_FLN:
            return "File Name";
        case PKT_TYPE_DAT:
            return "Data";
        case PKT_TYPE_WIN:
            return "Window Size";
        default:
            return "";
    }
}

struct packet retransmitpkt(uint32_t sequence) {
    struct packet ret = { 0 };

    ret.type = PKT_TYPE_REJ;
    ret.sequence = htonl(sequence);
    ret.size = 0;
    memset(ret.data, PKT_TYPE_REJ, PKT_DMAX);
    ret.checksum = in_cksum((unsigned short *)&ret, sizeof(ret));

    return ret;
}

struct packet ackpkt(const struct packet *src) {
    struct packet ret = { 0 };

    if (src != NULL) {
        memcpy(&ret, src, sizeof(*src));
        ret.type = PKT_TYPE_RR;
        ret.checksum = 0;
        ret.checksum = in_cksum((unsigned short *)&ret, sizeof(ret));
        memset(ret.data, PKT_TYPE_RR, PKT_DMAX);
    }

    return ret;
}

struct packet rrpkt(uint32_t sequence) {
    struct packet ret = { 0 };
    memset(&ret, PKT_TYPE_RR, sizeof(struct packet));
    ret.sequence = sequence;
    ret.checksum = 0;
    ret.checksum = in_cksum((unsigned short *)&ret, sizeof(struct packet));

    return ret;
}

struct packet rejpkt(uint32_t sequence) {
    struct packet ret = { 0 };
    memset(&ret, PKT_TYPE_REJ, sizeof(struct packet));
    ret.sequence = sequence;
    ret.checksum = 0;
    ret.checksum = in_cksum((unsigned short *)&ret, sizeof(struct packet));

    return ret;
}