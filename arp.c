/*
     This file (was) part of GNUnet.
     Copyright (C) 2018 Christian Grothoff

     GNUnet is free software: you can redistribute it and/or modify it
     under the terms of the GNU Affero General Public License as published
     by the Free Software Foundation, either version 3 of the License,
     or (at your option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Affero General Public License for more details.

     You should have received a copy of the GNU Affero General Public License
     along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file arp.c
 * @brief ARP tool
 * @author Christian Grothoff
 */
#include "glab.h"
#include "print.c"
#include <arpa/inet.h>

/**
 * gcc 4.x-ism to pack structures (to be used before structs);
 * Using this still causes structs to be unaligned on the stack on Sparc
 * (See #670578 from Debian).
 */
_Pragma("pack(push)") _Pragma ("pack(1)")

struct EthernetHeader {
    struct MacAddress dst;
    struct MacAddress src;

    /**
     * See ETH_P-values.
     */
    uint16_t tag;
};


/**
 * ARP header for Ethernet-IPv4.
 */
struct ArpHeaderEthernetIPv4 {
    /**
     * Must be #ARP_HTYPE_ETHERNET.
     */
    uint16_t htype;

    /**
     * Protocol type, must be #ARP_PTYPE_IPV4
     */
    uint16_t ptype;

    /**
     * HLEN.  Must be #MAC_ADDR_SIZE.
     */
    uint8_t hlen;

    /**
     * PLEN.  Must be sizeof (struct in_addr) (aka 4).
     */
    uint8_t plen;

    /**
     * Type of the operation.
     */
    uint16_t oper;

    /**
     * HW address of sender. We only support Ethernet.
     */
    struct MacAddress sender_ha;

    /**
     * Layer3-address of sender. We only support IPv4.
     */
    struct in_addr sender_pa;

    /**
     * HW address of target. We only support Ethernet.
     */
    struct MacAddress target_ha;

    /**
     * Layer3-address of target. We only support IPv4.
     */
    struct in_addr target_pa;
};

_Pragma("pack(pop)")


/**
 * Per-interface context.
 */
struct Interface {
    /**
     * MAC of interface.
     */
    struct MacAddress mac;

    /**
     * IPv4 address of interface (we only support one IP per interface!)
     */
    struct in_addr ip;

    /**
     * IPv4 netmask of interface.
     */
    struct in_addr netmask;

    /**
     * Name of the interface.
     */
    char *name;

    /**
     * Interface number.
     */
    uint16_t ifc_num;

    /**
     * MTU to enforce for this interface.
     */
    uint16_t mtu;
};


/**
 * Number of available contexts.
 */
static unsigned int num_ifc;

/**
 * All the contexts.
 */
static struct Interface *gifc;

////////////////////////////////////   added for work   ////////////////////////////////////
#include <time.h>
#define ARP_CACHE_SIZE 10
struct ArpEntry {
    struct in_addr ip;
    struct MacAddress mac;
    struct Interface ifc;
    time_t timestamp;
};
static struct ArpEntry arpCache[ARP_CACHE_SIZE];
static size_t arpCacheSize = 0;

/**
 * compare method for 2 mac-addresses, taken from faq-sheet Prof. Grothoff & Prof. Wenger
 * @param mac1 first mac-address
 * @param mac2 second mac-address
 * @return 0 if both are equal. any other value: they are not equal
 */
int maccomp (const struct MacAddress *mac1, const struct MacAddress *mac2) {
    return memcmp (mac1, mac2, sizeof(struct MacAddress));
};
/**
 * compare method for 2 ip-addresses
 * @param ip1 first ip-address
 * @param ip2 second ip-address
 * @return 0 if both are equal. any other value: they are not equal
 */
int ipcomp (struct in_addr *ip1, struct in_addr *ip2 ){
    return memcmp(ip1, ip2, sizeof(struct in_addr));
}
////////////////////////////////////////////////////////////////////////////////////////////

static void
print_mac (const struct MacAddress *mac)
{
    print ("%02x:%02x:%02x:%02x:%02x:%02x",
           mac->mac[0], mac->mac[1], mac->mac[2], mac->mac[3], mac->mac[4], mac->mac[5]);
}

static void
print_ip (const struct in_addr *ip)
{
    char buf[INET_ADDRSTRLEN];
    print ("%s", inet_ntop(AF_INET, ip, buf, sizeof (buf)));
}

/**
 * Forward @a frame to interface @a dst.
 *
 * @param dst target interface to send the frame out on
 * @param frame the frame to forward
 * @param frame_size number of bytes in @a frame
 */
static void
forward_to(struct Interface *dst, const void *frame, size_t frame_size) {
    char iob[frame_size + sizeof(struct GLAB_MessageHeader)];
    struct GLAB_MessageHeader hdr;

    if (frame_size > dst->mtu) abort();
    hdr.size = htons(sizeof(iob));
    hdr.type = htons(dst->ifc_num);
    memcpy(iob, &hdr, sizeof(hdr));
    memcpy(&iob[sizeof(hdr)], frame, frame_size);
    write_all(STDOUT_FILENO, iob, sizeof(iob));
}

/**
 * In cache of arp table search for the entry with the oldest timestamp (= the least recently used entry)
 * @return the index of the entry to be overwritten
 */

static int
search_oldest_entry() {
    time_t timestamp_persisted = arpCache[0].timestamp; //persist timestamp of first entry
    int override_index = 0;
    for (int i=1; i<arpCacheSize; i++) {
        if (timestamp_persisted > arpCache[i].timestamp) { //compare this entrys timestamp with the one persisted. if it is smaller, persist it.
            override_index = i;
        }
    }
    return override_index;
};

static void
lookup_and_add(struct ArpHeaderEthernetIPv4 arpRequest, struct Interface ifc) {

    /* lookup in cache and update */
    if(0 != ipcomp(&arpRequest.sender_pa, &ifc.ip)) { //check, if the reply was from ourself
        if(0 == ipcomp(&arpRequest.target_pa, &ifc.ip)) { //check, if the reply was meant for us
            int MAC_inCache = -1;
            int IP_inCache = -1;
            for (int j = 0; j < arpCacheSize; j++) {
                if (0 == maccomp(&arpCache[j].mac, &arpRequest.sender_ha)) {
                    MAC_inCache = j;
                    continue;
                }
                if (0 == ipcomp(&arpCache[j].ip, &arpRequest.sender_pa)) {
                    IP_inCache = j;
                }
            }

            if (0 <= MAC_inCache && 0 <= IP_inCache) { //there is an entry for the mac and the IP!
                arpCache[MAC_inCache].ip = arpRequest.sender_pa;
                arpCache[MAC_inCache].ifc = ifc;
                arpCache[MAC_inCache].timestamp = time(NULL);
                for (int j = IP_inCache; j < arpCacheSize; j++) { //fill up gap
                    arpCache[j] = arpCache[j + 1];
                }
                arpCacheSize--;
            } else if (0 <= MAC_inCache) { //there is an entry for the mac
                arpCache[MAC_inCache].ip = arpRequest.sender_pa;
                arpCache[MAC_inCache].ifc = ifc;
                arpCache[MAC_inCache].timestamp = time(NULL);
            } else if (0 <= IP_inCache) { //there is an entry for the IP
                arpCache[IP_inCache].ip = arpRequest.sender_pa;
                arpCache[IP_inCache].ifc = ifc;
                arpCache[IP_inCache].timestamp = time(NULL);
            } else { //or add a new entry
                size_t addPosition = 0;
                if (ARP_CACHE_SIZE > arpCacheSize) {
                    addPosition = arpCacheSize;
                } else {
                    addPosition = (size_t) search_oldest_entry();
                }
                struct ArpEntry newEntry;
                newEntry.ip = arpRequest.sender_pa;
                newEntry.mac = arpRequest.sender_ha;
                newEntry.ifc = ifc;
                newEntry.timestamp = time(NULL);
                arpCache[addPosition] = newEntry;
                arpCacheSize++;
            }
        } else {
            fprintf(stderr, "Odd, received ARP reply that is not for me!");
        }
    }
}

static void
send_response(struct ArpHeaderEthernetIPv4 arpRequest, struct Interface ifc) {

    for (int i = 0; i < num_ifc; i++) {
        if (0 == ipcomp(&arpRequest.target_pa, &gifc[i].ip)) {

            struct EthernetHeader ethHeader;
            ethHeader.src = ifc.mac;
            ethHeader.dst = arpRequest.sender_ha;
            ethHeader.tag = htons(0x0806);

            struct ArpHeaderEthernetIPv4 arpHeader;

            arpHeader.htype = htons(1);
            arpHeader.ptype = htons(0x0800);
            arpHeader.hlen = MAC_ADDR_SIZE;
            arpHeader.plen = 0x04;
            arpHeader.oper = htons(2);
            arpHeader.sender_ha = ifc.mac;
            arpHeader.sender_pa = ifc.ip;
            arpHeader.target_ha = arpRequest.sender_ha;
            arpHeader.target_pa = arpRequest.sender_pa;

            uint8_t sendBuff[sizeof(struct EthernetHeader) + sizeof(struct ArpHeaderEthernetIPv4)];
            memcpy(&sendBuff, &ethHeader, sizeof(ethHeader));
            memcpy(&sendBuff[sizeof ethHeader], &arpHeader, sizeof(arpHeader));

            struct Interface *ifc2 = &ifc;
            forward_to(ifc2, &sendBuff, sizeof(sendBuff));
        }
    }
}


/**
 * Parse and process frame received on @a ifc.
 *
 * @param ifc interface we got the frame on
 * @param frame raw frame data
 * @param frame_size number of bytes in @a frame
 */
static void
parse_frame(struct Interface *ifc, const void *frame, size_t frame_size) {

    struct EthernetHeader eh;
    const char *cframe = frame;


    if (frame_size < sizeof(eh)) {
        fprintf(stderr, "Malformed frame\n");
        return;
    }
    memcpy(&eh, frame, sizeof(eh));

    /////////   check if the header is a arp header /////////
    if (0x0806 == ntohs(eh.tag)){
        struct ArpHeaderEthernetIPv4 arpRequest;
        memcpy(&arpRequest, &cframe[sizeof(eh)], frame_size - sizeof(eh));

        struct Interface interface = *ifc;

        if (2 == ntohs(arpRequest.oper)) { //Is is a reply?
            lookup_and_add(arpRequest, interface);
        }
        else if (1 == ntohs(arpRequest.oper)) { // Is it a request
            send_response(arpRequest, interface);
        }

    } else {
        fprintf(stderr,"Unsupported Ethernet tag %04x\n", (eh.tag));
    }
}



/**
 * Process frame received from @a interface.
 *
 * @param interface number of the interface on which we received @a frame
 * @param frame the frame
 * @param frame_size number of bytes in @a frame
 */
static void
handle_frame(uint16_t interface, const void *frame, size_t frame_size) {
    if (interface > num_ifc) abort();
    parse_frame(&gifc[interface - 1], frame, frame_size);
}

static void
print_arp_cache(){
    for (int i=0; i < arpCacheSize; i++) {
        print_ip(&arpCache[i].ip);
        print(" -> ");
        print_mac(&arpCache[i].mac);
        print(" (");
        char *name = arpCache[i].ifc.name;
        for (int j = 0; arpCache[i].ifc.name[j] != NULL; j++) {
            print("%c", name[j]);
        }
        print(")\n");
    }
}

static int
ipv4_lookup(struct in_addr ip4){
    for (int i = 0; i < arpCacheSize; i++) {
        if (0== ipcomp(&ip4, &arpCache[i].ip)) {
            print_mac(&arpCache[i].mac);
            print("\n");
            return 0;
        }
    }
    return -1;
}


/**
 * The user entered an "arp" command.  The remaining
 * arguments can be obtained via 'strtok()'.
 */
static void
process_cmd_arp() {
    const char *tok = strtok(NULL, " ");
    struct in_addr v4;
    struct MacAddress mac;
    struct Interface *ifc;

    if (NULL == tok) {
        print_arp_cache();
        return;
    }
    if (1 != inet_pton(AF_INET, tok, &v4)) {
        fprintf(stderr, "`%s' is not a valid IPv4 address\n", tok);
        return;
    }
    tok = strtok(NULL, " ");
    if (NULL == tok) {
        fprintf(stderr, "No network interface provided\n");
        return;
    }
    ifc = NULL;
    for (unsigned int i = 0; i < num_ifc; i++) {
        if (0 == strcasecmp(tok, gifc[i].name)) {
            ifc = &gifc[i];
            break;
        }
    }
    if (NULL == ifc) {
        fprintf(stderr, "Interface `%s' unknown\n", tok);
        return;
    }
    if (-1 == ipv4_lookup(v4)) {
        struct MacAddress bcMac = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        struct MacAddress unknownMac = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        struct EthernetHeader ethHeader;
        ethHeader.src = ifc->mac;
        ethHeader.dst = bcMac;
        ethHeader.tag = htons(0x0806);

        struct ArpHeaderEthernetIPv4 arpHeader;
        arpHeader.htype = htons(1);
        arpHeader.ptype = htons(0x0800);
        arpHeader.hlen = MAC_ADDR_SIZE;
        arpHeader.plen = 0x04;
        arpHeader.oper = htons(1);
        arpHeader.sender_ha = ifc->mac;
        arpHeader.sender_pa = ifc->ip;
        arpHeader.target_ha = unknownMac;
        arpHeader.target_pa = v4;

        uint8_t sendBuff[sizeof(struct EthernetHeader) + sizeof(struct ArpHeaderEthernetIPv4)];
        memcpy(&sendBuff, &ethHeader, sizeof(ethHeader));
        memcpy(&sendBuff[sizeof ethHeader], &arpHeader, sizeof(arpHeader));
        forward_to(ifc, &sendBuff, sizeof(sendBuff));
    }
}


/**
 * Parse network specification in @a net, initializing @a ifc.
 * Format of @a net is "IPV4:IP/NETMASK".
 *
 * @param ifc[out] interface specification to initialize
 * @param arg interface specification to parse
 * @return 0 on success
 */
static int
parse_network(struct Interface *ifc, const char *net) {
    const char *tok;
    char *ip;
    unsigned int mask;

    if (0 != strncasecmp(net, "IPV4:", strlen("IPV4:"))) {
        fprintf(stderr, "Interface specification `%s' does not start with `IPV4:'\n", net);
        return 1;
    }
    net += strlen("IPV4:");
    tok = strchr(net, '/');
    if (NULL == tok) {
        fprintf(stderr, "Error in interface specification `%s': lacks '/'\n", net);
        return 1;
    }
    ip = strndup(net, tok - net);
    if (1 != inet_pton(AF_INET, ip, &ifc->ip)) {
        fprintf(stderr, "IP address `%s' malformed\n", ip);
        free(ip);
        return 1;
    }
    free(ip);
    tok++;
    if (1 != sscanf(tok, "%u", &mask)) {
        fprintf(stderr, "Netmask `%s' malformed\n", tok);
        return 1;
    }
    if (mask > 32) {
        fprintf(stderr, "Netmask invalid (too large)\n");
        return 1;
    }
    ifc->netmask.s_addr = htonl(~(uint32_t) ((1LLU << (32 - mask)) - 1LLU));
    return 0;
}


/**
 * Parse interface specification @a arg and update @a ifc.  Format is
 * "IFCNAME[IPV4:IP/NETMASK]=MTU".  The "=MTU" is optional.
 *
 * @param ifc[out] interface specification to initialize
 * @param arg interface specification to parse
 * @return 0 on success
 */
static int
parse_cmd_arg(struct Interface *ifc, const char *arg) {
    const char *tok;
    char *nspec;

    ifc->mtu = 1500 + sizeof(struct EthernetHeader); /* default in case unspecified */
    tok = strchr(arg, '[');
    if (NULL == tok) {
        fprintf(stderr, "Error in interface specification: lacks '['");
        return 1;
    }
    ifc->name = strndup(arg, tok - arg);
    arg = tok + 1;
    tok = strchr(arg, ']');
    if (NULL == tok) {
        fprintf(stderr, "Error in interface specification: lacks ']'");
        return 1;
    }
    nspec = strndup(arg, tok - arg);
    if (0 != parse_network(ifc, nspec)) {
        free(nspec);
        return 1;
    }
    free(nspec);
    arg = tok + 1;
    if ('=' == arg[0]) {
        unsigned int mtu;

        if (1 != (sscanf(&arg[1], "%u", &mtu))) {
            fprintf(stderr, "Error in interface specification: MTU not a number\n");
            return 1;
        }
        if (mtu < 400) {
            fprintf(stderr, "Error in interface specification: MTU too small\n");
            return 1;
        }
        ifc->mtu = mtu + sizeof(struct EthernetHeader);
    }
    return 0;
}


/**
 * Handle control message @a cmd.
 *
 * @param cmd text the user entered
 * @param cmd_len length of @a cmd
 */
static void
handle_control(char *cmd, size_t cmd_len) {
    const char *tok;

    cmd[cmd_len - 1] = '\0';
    tok = strtok(cmd, " ");
    if (0 == strcasecmp(tok, "arp")) process_cmd_arp();
    else fprintf(stderr, "Unsupported command `%s'\n", tok);
}


/**
 * Handle MAC information @a mac
 *
 * @param ifc_num number of the interface with @a mac
 * @param mac the MAC address at @a ifc_num
 */
static void
handle_mac(uint16_t ifc_num, const struct MacAddress *mac) {
    if (ifc_num > num_ifc) abort();
    gifc[ifc_num - 1].mac = *mac;
}


#include "loop.c"

/**
 * Launches the arp tool.
 *
 * @param argc number of arguments in @a argv
 * @param argv binary name, followed by list of interfaces to switch between
 * @return not really
 */
int
main(int argc, char **argv) {
    struct Interface ifc[argc];

    memset(ifc, 0, sizeof(ifc));
    num_ifc = argc - 1;
    gifc = ifc;
    for (unsigned int i = 1; i < argc; i++) {
        struct Interface *p = &ifc[i - 1];

        ifc[i - 1].ifc_num = i;
        if (0 != parse_cmd_arg(p, argv[i])) abort();
    }
    loop();
    for (unsigned int i = 1; i < argc; i++)
        free(ifc[i - 1].name);
    return 0;
}
