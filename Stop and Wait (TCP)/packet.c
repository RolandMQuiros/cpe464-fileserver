#include <arpa/inet.h>
#include <string.h>

#include "packet.h"
#include "select_call.h"
#include "cpe464.h"

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
        memset(ret.data, PKT_TYPE_RR, 
    }
    
    return ret;
}
