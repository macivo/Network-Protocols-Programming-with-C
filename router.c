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
 * @file router.c
 * @brief IPv4 router
 * @author Christian Grothoff
 */
#include "glab.h"
#include "print.c"
#include "crc.c"


/* see http://www.iana.org/assignments/ethernet-numbers */
#ifndef ETH_P_IPV4
/**
 * Number for IPv4
 */
#define ETH_P_IPV4 0x0800
#endif

#ifndef ETH_P_ARP
/**
 * Number for ARP
 */
#define ETH_P_ARP 0x0806
#endif


/**
 * gcc 4.x-ism to pack structures (to be used before structs);
 * Using this still causes structs to be unaligned on the stack on Sparc
 * (See #670578 from Debian).
 */
_Pragma("pack(push)") _Pragma("pack(1)")

struct EthernetHeader
{
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
struct ArpHeaderEthernetIPv4
{
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


/* some systems use one underscore only, and mingw uses no underscore... */
#ifndef __BYTE_ORDER
#ifdef _BYTE_ORDER
#define __BYTE_ORDER _BYTE_ORDER
#else
#ifdef BYTE_ORDER
#define __BYTE_ORDER BYTE_ORDER
#endif
#endif
#endif
#ifndef __BIG_ENDIAN
#ifdef _BIG_ENDIAN
#define __BIG_ENDIAN _BIG_ENDIAN
#else
#ifdef BIG_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#endif
#endif
#endif
#ifndef __LITTLE_ENDIAN
#ifdef _LITTLE_ENDIAN
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#else
#ifdef LITTLE_ENDIAN
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#endif
#endif
#endif


#define IP_FLAGS_RESERVED 1
#define IP_FLAGS_DO_NOT_FRAGMENT 2
#define IP_FLAGS_MORE_FRAGMENTS 4
#define IP_FLAGS 7

#define IP_FRAGMENT_MULTIPLE 8

/**
 * Standard IPv4 header.
 */
struct IPv4Header
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned int header_length : 4;
    unsigned int version : 4;
#elif __BYTE_ORDER == __BIG_ENDIAN
    unsigned int version : 4;
  unsigned int header_length : 4;
#else
  #error byteorder undefined
#endif
    uint8_t diff_serv;

    /**
     * Length of the packet, including this header.
     */
    uint16_t total_length;

    /**
     * Unique random ID for matching up fragments.
     */
    uint16_t identification;

    /**
     * Fragmentation flags and fragmentation offset.
     */
    uint16_t fragmentation_info;

    /**
     * How many more hops can this packet be forwarded?
     */
    uint8_t ttl;

    /**
     * L4-protocol, for example, IPPROTO_UDP or IPPROTO_TCP.
     */
    uint8_t protocol;

    /**
     * Checksum.
     */
    uint16_t checksum;

    /**
     * Origin of the packet.
     */
    struct in_addr source_address;

    /**
     * Destination of the packet.
     */
    struct in_addr destination_address;
};


#define ICMPTYPE_DESTINATION_UNREACHABLE 3
#define ICMPTYPE_TIME_EXCEEDED 11

#define ICMPCODE_NETWORK_UNREACHABLE 0
#define ICMPCODE_HOST_UNREACHABLE 1
#define ICMPCODE_FRAGMENTATION_REQUIRED 4

/**
 * ICMP header.
 */
struct IcmpHeader
{
    uint8_t type;
    uint8_t code;
    uint16_t crc;

    union
    {
        /**
         * Payload for #ICMPTYPE_DESTINATION_UNREACHABLE (RFC 1191)
         */
        struct ih_pmtu
        {
            uint16_t empty;
            uint16_t next_hop_mtu;
        } destination_unreachable;

        /**
         * Unused bytes for #ICMPTYPE_TIME_EXCEEDED.
         */
        uint32_t time_exceeded_unused;

    } quench;

    /* followed by original IP header + first 8 bytes of original IP datagram
       (at least for the two ICMP message types we care about here) */

};
_Pragma("pack(pop)")


/**
 * Per-interface context.
 */
struct Interface
{
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
     * Number of this interface.
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


_Pragma("pack(push)") _Pragma("pack(1)")

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

struct in_addr ON_LINK_GATEWAY;
struct in_addr STANDARD_GATEWAY;

struct Routing_entry *routing_table;

struct Routing_entry {
    struct in_addr network_target;
    struct in_addr network_mask;
    struct in_addr gateway;
    struct Interface ifc;
    int undeleteable;
    struct Routing_entry *next;

};


_Pragma("pack(pop)")

struct in_addr IP0;
struct MacAddress NULL_ADDRESS = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static struct Routing_entry* lookup_routing_entry_rt(struct Routing_entry looked_up_entry);
static struct Routing_entry* lookup_rt(struct in_addr *ipv4);
static void send_broadcast_APR(struct Interface *ifc, struct in_addr target_IP);
static void learn_arp(struct in_addr ip, struct MacAddress mac, struct Interface ifc);
static void handle_arp_request(struct ArpHeaderEthernetIPv4 arpRequest, struct Interface ifc);
static void lookup_and_add_ARP(struct ArpHeaderEthernetIPv4 arpRequest, struct Interface ifc);
static struct MacAddress lookup_ipv4_inARP (struct in_addr ip4);
static int check_fragmentation(struct EthernetHeader *eh, struct IPv4Header *ip, struct Interface *ifc, void *payload, size_t payloadsize);

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

static void
print_mac (const struct MacAddress *mac)
{
    print ("%02x:%02x:%02x:%02x:%02x:%02x",
           mac->mac[0], mac->mac[1], mac->mac[2], mac->mac[3], mac->mac[4], mac->mac[5]);
}

static void
fprintf_mac (const struct MacAddress *mac)
{
    fprintf(stderr,"%02x:%02x:%02x:%02x:%02x:%02x",
           mac->mac[0], mac->mac[1], mac->mac[2], mac->mac[3], mac->mac[4], mac->mac[5]);
}

static void
print_ip (const struct in_addr *ip)
{
    char buf[INET_ADDRSTRLEN];
    print("%s", inet_ntop(AF_INET, ip, buf, sizeof (buf)));
}

static void
fprintf_ip (const struct in_addr *ip)
{
    char buf[INET_ADDRSTRLEN];
    fprintf(stderr,"%s", inet_ntop(AF_INET, ip, buf, sizeof (buf)));
}
////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Forward @a frame to interface @a dst.
 *
 * @param dst target interface to send the frame out on
 * @param frame the frame to forward
 * @param frame_size number of bytes in @a frame
 */
static void
forward_to (struct Interface *dst,
            const void *frame,
            size_t frame_size)
{
    char iob[frame_size + sizeof (struct GLAB_MessageHeader)];
    struct GLAB_MessageHeader hdr;

    if (frame_size > dst->mtu)
        abort ();
    hdr.size = htons (sizeof (iob));
    hdr.type = htons (dst->ifc_num);
    memcpy (iob,
            &hdr,
            sizeof (hdr));
    memcpy (&iob[sizeof (hdr)],
            frame,
            frame_size);
    write_all (STDOUT_FILENO,
               iob,
               sizeof (iob));
}


/**
 * Create Ethernet frame and forward it via @a ifc to @a target_ha.
 *
 * @param ifc interface to send frame out on
 * @param target destination MAC
 * @param tag Ethernet tag to use
 * @param frame_payload payload to use in frame
 * @param frame_payload_size number of bytes in @a frame_payload
 */
static void
forward_frame_payload_to (struct Interface *ifc,
                          const struct MacAddress *target_ha,
                          uint16_t tag,
                          const void *frame_payload,
                          size_t frame_payload_size)
{

    char frame[sizeof (struct EthernetHeader) + frame_payload_size];
    struct EthernetHeader eh;

    if (frame_payload_size + sizeof (struct EthernetHeader) > ifc->mtu)
        abort ();
    eh.dst = *target_ha;
    eh.src = ifc->mac;
    eh.tag = ntohs (tag);
    memcpy (frame,
            &eh,
            sizeof (eh));
    memcpy (&frame[sizeof (eh)],
            frame_payload,
            frame_payload_size);
    forward_to (ifc,
                frame,
                sizeof (frame));
}
static int size_rt;



struct Routing_entry* create_entry(struct Routing_entry *entry) {


    struct Routing_entry* new_Routing_entry = malloc(sizeof(struct Routing_entry));


    if (NULL != new_Routing_entry){
        new_Routing_entry->gateway = entry->gateway;
        new_Routing_entry->network_target = entry->network_target;
        new_Routing_entry->network_mask = entry->network_mask;
        new_Routing_entry->ifc = entry->ifc;
        new_Routing_entry->undeleteable = 0;
        new_Routing_entry->next = NULL;

    }else {
        exit(1);
    }

    return new_Routing_entry;
}

void delete_entry(struct Routing_entry *looked_up_entry) {

    struct Routing_entry *iter = routing_table;
    for (iter; NULL != iter; iter = iter->next){
        if (iter->next == looked_up_entry) {
            struct Routing_entry* current_entry = iter;
            struct Routing_entry* freed_entry = iter->next;
            current_entry->next = iter->next->next;
            free(freed_entry);
            return;
        }

    }
}



/**
 * Adds a new entry to the routing table
 * @param entry
 * @return
 */
static struct Routing_entry*
add_entry(struct Routing_entry *entry) {
    struct Routing_entry *looked_up_entry = lookup_routing_entry_rt(*entry); //returns the value that is BEFORE the new entry in routing table
    struct Routing_entry *new_entry = create_entry(entry); //create a new routing entry

    if(NULL == looked_up_entry->next) { //if there is only one entry so far, next entry is null
            looked_up_entry->next = new_entry;
    } else { //if there is so far more than 1 entry in routing table
        new_entry->next = looked_up_entry->next;
        looked_up_entry->next = new_entry;
    }

    return new_entry;
}


static int
init_router() {


    struct Routing_entry entry;
    int netmask_val = gifc[0].netmask.s_addr;
    int index = 0;
    for(int j = 1; j<num_ifc; j++) {
        int netmask_val2 = gifc[j].netmask.s_addr;
        if (netmask_val2 < netmask_val) {
            netmask_val = netmask_val2;
            index = j;
        }
    }


    entry.network_mask = gifc[index].netmask;
    struct in_addr network_IP;
    network_IP.s_addr = gifc[index].ip.s_addr & gifc[index].netmask.s_addr;
    entry.network_target = network_IP;
    entry.ifc = gifc[index];
    entry.gateway = IP0;
    entry.undeleteable = 1;

    routing_table = create_entry(&entry);



    for(int i = 0; i<num_ifc; i++){
        if (i != index) {
            struct Routing_entry entry;
            entry.network_mask = gifc[i].netmask;
            struct in_addr network_IP;
            network_IP.s_addr = gifc[i].ip.s_addr & gifc[i].netmask.s_addr;
            entry.network_target = network_IP;
            entry.ifc = gifc[i];
            entry.gateway = IP0;
            entry.undeleteable=1;
            add_entry(&entry);

        }
    }
    return &entry;
}


static void
send_ICMP_message(int ICMP_type, int ICMP_code, struct EthernetHeader eh, struct IPv4Header ipv4h, struct Interface *ifc, void *payload, size_t payload_size) {

    struct IcmpHeader icmp;
    icmp.code = ICMP_code;
    icmp.crc = 0;


    if (3 == ICMP_type){ //ICMP Type 3
        icmp.quench.destination_unreachable.empty = 0;
        icmp.quench.destination_unreachable.next_hop_mtu = 0;
        icmp.type = ICMP_type;
    } else { //ICMP Type 11
        icmp.quench.time_exceeded_unused = 0;
        icmp.type = ICMP_type;
    }


    struct EthernetHeader etherneth;
    etherneth.src = ifc->mac;
    etherneth.dst = eh.src;
    etherneth.tag = htons(ETH_P_IPV4);

    size_t size_icmp_whole = sizeof(icmp) + sizeof(ipv4h) + payload_size;

    struct IPv4Header ip_header;
    ip_header.destination_address.s_addr = ipv4h.source_address.s_addr;
    ip_header.source_address.s_addr = ifc->ip.s_addr;
    ip_header.total_length = ip_header.header_length + sizeof(icmp) + size_icmp_whole;
    ip_header.protocol = 1;
    ip_header.version = 4;
    ip_header.diff_serv = 0;
    ip_header.fragmentation_info = 0;
    ip_header.identification = 0;
    ip_header.header_length = ipv4h.header_length;
    ip_header.ttl = 64; //recommended initial value for ttl

    ip_header.checksum = 0;
    ip_header.checksum = GNUNET_CRYPTO_crc16_n(&ip_header, sizeof(ip_header));

    char frame_buff[UINT16_MAX];
    void* frame = frame_buff;
    size_t frame_size = sizeof(etherneth) + sizeof(ip_header) + sizeof(icmp) + sizeof(ipv4h) + payload_size;

    icmp.crc = GNUNET_CRYPTO_crc16_n(frame + sizeof(etherneth) + sizeof(ip_header), size_icmp_whole);


    memcpy(frame, &etherneth, sizeof(etherneth));
    memcpy(frame + sizeof(etherneth), &ip_header, sizeof(ip_header));
    memcpy(frame + sizeof(etherneth) + sizeof(ip_header), &icmp, sizeof(icmp));
    memcpy(frame + sizeof(etherneth) + sizeof(ip_header) + sizeof(icmp), &ipv4h, sizeof(ipv4h));
    memcpy(frame + sizeof(etherneth) + sizeof(ip_header) + sizeof(icmp) + sizeof(ipv4h), payload, payload_size);

    forward_to(ifc, frame, frame_size);

}

/**
 * checkup whether there are other entries with the same network target. These entries have to be ordered by the size of their networkmasks within
 * their blocks with the same network targets. This function will return the entry that is before the new entry given as parameter
 * @param looked_up_entry a new entry that will be added
 * @return an entry that has to be placed before new entry according to an ascending order in terms of network mask
 */
static struct Routing_entry*
lookup_routing_entry_rt(struct Routing_entry looked_up_entry) {
    struct Routing_entry *iter = routing_table;
    struct Routing_entry *saved_value = iter;

    for (iter; NULL != iter; iter = iter->next){
        if (0 == ipcomp(&iter->network_target, &looked_up_entry.network_target)) { //has the entry the same network target
            int val_looked_up = looked_up_entry.network_mask.s_addr;
            int val_iter = iter->network_mask.s_addr;
            if  (val_looked_up > val_iter) { //lookup whether the net mask of the new entry is smaller than the net mask of the entry currently looked at
                return saved_value;
            }
            saved_value = iter; //save this entry of the same network target
        }
    }

    return saved_value;
}

static struct Routing_entry*
lookup_rt_for_del(struct in_addr target_network, struct in_addr target_netmask, struct in_addr next_hop, struct Interface *ifc) {
    struct Routing_entry *iter = routing_table;
    for (iter; NULL != iter; iter = iter->next){
        if (0 == ipcomp(&iter->network_target, &target_network) && (0 == ipcomp(&iter->network_mask, &target_netmask))){
            if ((0 == iter->undeleteable) && (0 == ipcomp(&iter->gateway, &next_hop))) {
                return iter;
            }
        }
    }
    return NULL;

}


static struct Routing_entry*
lookup_rt(struct in_addr *ipv4) {

    struct Routing_entry *iter = routing_table;
    for (iter; NULL != iter; iter = iter->next){
        if (0 == ipcomp(&iter->network_target, ipv4)) {
            return iter;
        }
    }
    return NULL;
}


/**
 * Route the @a ip packet with its @a payload.
 *
 * @param origin interface we received the packet from
 * @param ip IP header
 * @param payload IP packet payload
 * @param payload_size number of bytes in @a payload
 */
static void
route (struct IPv4Header ip,
       struct Interface *ifc,
       struct EthernetHeader *eh,
       void *payload,
       size_t payload_size)
{

    int checked = GNUNET_CRYPTO_crc16_n(&ip, sizeof(struct IPv4Header));
    struct Interface interface = *ifc;

    if(0!=checked){
        fprintf (stderr,"cyclic redundancy checksum ERROR!\n");
        return;
    }


    if(1>ip.ttl) { //ttl is 0, the frame can not be processed. send ICMP Message "TTL exceeded" (type 11)
        send_ICMP_message(ICMPTYPE_TIME_EXCEEDED, 0, *eh, ip, ifc, payload, payload_size);
        return;
    }//else : ttl is >= 1, frame can be processed

    // IPV4 address received AND netmask results in networkaddress
    struct in_addr net; //network address
    net.s_addr = ip.destination_address.s_addr & ifc->netmask.s_addr;

    //look up the network address in routing

    struct in_addr gateway; //next hop / gateway
    struct Routing_entry* looked_up_node = lookup_rt(&net); //The node where the gateway can be found in routing table

    struct Interface routing_ifc; //the destination Interface


    eh->tag = htons(ETH_P_IPV4);

    if (NULL != looked_up_node) { //network address was found in table, gateway address is known

        routing_ifc = looked_up_node->ifc;
        eh->src = routing_ifc.mac;

        gateway = looked_up_node->gateway; //save the gateway found in routingtable

        if(0 == ipcomp(&IP0, &gateway)) { // gateway address is 0.0.0.0 = on-link, we take the dst-IP-Address of IPv4-Header as dst-Address
            gateway = ip.destination_address;
        } //if the gateway address is not 0.0.0.0 / not in our own network. Therefore we use the gateway saved before


    }else { //no network address was found in table

        struct Routing_entry* looked_up_node2 = lookup_rt(&IP0); // is there a standard gateway?

        if (NULL != looked_up_node2) { //if a standard gateway was found in routing table

            eh->src = looked_up_node2->ifc.mac;

            gateway = looked_up_node2->gateway; //save the standard gateway found in routingtable
            routing_ifc = looked_up_node2->ifc;

            fprintf_ip(&looked_up_node2->gateway);


        } else { //if no standard gateway was found in routing table

            send_ICMP_message(ICMPTYPE_DESTINATION_UNREACHABLE, ICMPCODE_NETWORK_UNREACHABLE, *eh, ip, ifc, payload, payload_size);
            fprintf(stderr, "Dropping ICMP packet: next hop MAC unknown\n");
            return;
        }

    }


    struct MacAddress new_MAC = lookup_ipv4_inARP(gateway); //lookup the address in "gateway" in ARP to find fitting MAC-address

    if(0 == maccomp(&new_MAC, &NULL_ADDRESS)) { //if the IP has NOT been found in ARP-table, the received MAC is the NULL_ADDRESS (00:00:00:00:00:00)

        send_broadcast_APR(ifc, gateway); //it has to be sent as ARP broadcast

        struct MacAddress new_MAC2 = lookup_ipv4_inARP(gateway); //new Mac Address to be used as destination is new_MAC2
        if(0 == maccomp(&new_MAC, &NULL_ADDRESS)) { //broadcast was unsuccessful, ARP entry not found!
            send_ICMP_message(ICMPTYPE_DESTINATION_UNREACHABLE, ICMPCODE_HOST_UNREACHABLE, *eh, ip, ifc, payload, payload_size);
            fprintf(stderr, "Dropping ICMP packet: no route to host\n");
            return;
        } else {
            eh->dst = new_MAC2; // save the found MAC-address as new destination in Ethernet Header if it is NOT null
        }
    } else { //the IP has been found in ARP-table. the MAC-address was saved

        eh->dst = new_MAC;

    }

    int bit = check_fragmentation(eh, &ip, ifc, payload, payload_size);

    /**
     * Case: Payload has to be fragmented
     */
    if (0 == bit) { //it's fragmented (payload size is > MTU) and fragmentation is allowed (fragmentation flag is 0)
        int fragmentsize = (ifc->mtu)-14-20; //From Maximum Transmission Unit the size of Ethernet header (14) and size of IPv4 Header must be subtracted
        int modulo = payload_size%fragmentsize;
        int no_fragments = payload_size/fragmentsize;

        if (0 != modulo){
            no_fragments++;

        }

        uint16_t frag_big_endian; //set fragmentation info to big endian, so that
        // first bit = reserved bit, second bit = fragmentation bit, third bit = more fragments; other 13 bits: Fragment offset

        /**
         * Iterate through framents if framentation is necessary and allowed
         */
        for(int j = 0; j < no_fragments; j++) { //iterate through all fragments. j = packetnumber
            int offset = j*(fragmentsize/8);
            ip.identification++;
            if (j==no_fragments-1) {
                frag_big_endian &= ~(1UL << 13); //set 3rd bit to 0: last fragment
            } else {
                frag_big_endian |= 1UL << 13; //set 3rd bit to 1: there are more fragments following
            }
            frag_big_endian = frag_big_endian+offset;
            ip.fragmentation_info = htons(frag_big_endian);


            //reduce TTL by 1
            ip.ttl--;
            //create new checksum in IPv4-header
            ip.checksum = 0;
            int checksum_renewed = GNUNET_CRYPTO_crc16_n(&ip, sizeof(struct IPv4Header));
            ip.checksum = checksum_renewed;


            uint8_t sendBuff[sizeof(struct EthernetHeader) + sizeof(ip) + fragmentsize];
            memcpy(&sendBuff, &eh, sizeof(eh));
            memcpy(&sendBuff[sizeof(struct EthernetHeader)], &ip, sizeof(ip));

            if (0 !=modulo && j==no_fragments-1) { //check, if it is the last packet & if there was a remainder from payload_size%fragmentsize (= "modulo")
                //if this is the case, do not use full fragment size, but only remainder
                memcpy(&sendBuff[sizeof(struct EthernetHeader)+ sizeof(ip)], &payload+offset, modulo);
            } else {
                memcpy(&sendBuff[sizeof(struct EthernetHeader) + sizeof(ip)], &payload + offset, fragmentsize);
            }
            forward_to(&routing_ifc, &sendBuff, sizeof(sendBuff));
        }

    /**
     * Case: Payload NOT fragmented
     */
    } else { //if it's not fragmented

        //reduce TTL by 1
        ip.ttl--;
        //create new checksum in IPv4-header
        ip.checksum = 0;
        int checksum_renewed = GNUNET_CRYPTO_crc16_n(&ip, sizeof(struct IPv4Header));
        ip.checksum = checksum_renewed;


        char bufferFrame[sizeof(struct EthernetHeader) + sizeof(struct IPv4Header) + payload_size];
        memcpy(&bufferFrame, eh, sizeof(struct EthernetHeader));
        memcpy(&bufferFrame[sizeof(struct EthernetHeader)], &ip, sizeof(struct IPv4Header));
        memcpy(&bufferFrame[sizeof(struct EthernetHeader) + sizeof(struct IPv4Header)], payload, payload_size);

      forward_to(&routing_ifc, &bufferFrame, (sizeof(struct EthernetHeader) + sizeof(struct IPv4Header) + payload_size));

    }


}

static int
check_fragmentation(struct EthernetHeader *eh, struct IPv4Header *ip, struct Interface *ifc, void *payload, size_t payloadsize) {



    if(ifc->mtu-14-20 < payloadsize) { //From Maximum Transmission Unit the size of Ethernet header (14) and IPv4 Header must be subtracted
        int bit = (ip->fragmentation_info >> 6) & 1U; //check 2nd bit
        if (0 == bit) { //fragmentation allowed
            return 0;
        }
        if (1 == bit) { //fragmentation not allowed
            send_ICMP_message(ICMPTYPE_DESTINATION_UNREACHABLE, ICMPCODE_FRAGMENTATION_REQUIRED, *eh, *ip, ifc, payload, payloadsize);
        }
    }
    return 1;

}

static struct MacAddress
lookup_ipv4_inARP (struct in_addr ip4){

    for (int i = 0; i < arpCacheSize; i++) {
        if (0== ipcomp(&ip4, &arpCache[i].ip)) {
            return arpCache[i].mac;
        }
    }
    return NULL_ADDRESS;
}


// same as send_response in arp.c //added by Mac
static void handle_arp_request(struct ArpHeaderEthernetIPv4 arpRequest, struct Interface ifc) {
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

            forward_to(&ifc, &sendBuff, sizeof(sendBuff));
        }
    }
}

// from arp.c //added by Mac
/**
 * In cache of arp table search for the entry with the oldest timestamp (= the least recently used entry)
 * @return the index of the entry to be overwritten
 */
static int search_oldest_entry() {
    time_t timestamp_persisted = arpCache[0].timestamp; //persist timestamp of first entry
    int override_index = 0;
    for (int i=1; i<arpCacheSize; i++) {
        if (timestamp_persisted > arpCache[i].timestamp) { //compare this entrys timestamp with the one persisted. if it is smaller, persist it.
            override_index = i;
        }
    }
    return override_index;
};

static void learn_arp(struct in_addr ip, struct MacAddress mac, struct Interface ifc) {
    int MAC_inCache = -1;
    int IP_inCache = -1;
    for (int j = 0; j < arpCacheSize; j++) {
        if (0 == maccomp(&arpCache[j].mac, &mac)) {
            MAC_inCache = j;
            continue;
        }
        if (0 == ipcomp(&arpCache[j].ip, &ip)) {
            IP_inCache = j;
        }
    }
    if (0 <= MAC_inCache && 0 <= IP_inCache) { //there is an entry for the mac and the IP!
        arpCache[MAC_inCache].ip = ip;
        arpCache[MAC_inCache].ifc = ifc;
        arpCache[MAC_inCache].timestamp = time(NULL);

        for (int j = IP_inCache; j < arpCacheSize; j++) { //fill up gap
            arpCache[j] = arpCache[j + 1];
        }
        arpCacheSize--;
    } else if (0 <= MAC_inCache) { //there is an entry for the mac
        arpCache[MAC_inCache].ip = ip;
        arpCache[MAC_inCache].ifc = ifc;
        arpCache[MAC_inCache].timestamp = time(NULL);
    } else if (0 <= IP_inCache) { //there is an entry for the IP
/*       TODO: // Korrektur update mac statt ip???     */
        arpCache[IP_inCache].mac = mac;
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
        newEntry.ip = ip;
        newEntry.mac = mac;
        newEntry.ifc = ifc;
        newEntry.timestamp = time(NULL);
        arpCache[addPosition] = newEntry;
        arpCacheSize++;
    }
}

// from arp.c //added by Mac
static void lookup_and_add_ARP(struct ArpHeaderEthernetIPv4 arpRequest, struct Interface ifc) {
    /* lookup in cache and update */
    if(0 != ipcomp(&arpRequest.sender_pa, &ifc.ip)) { //check, if the reply was from ourself
        if(0 == ipcomp(&arpRequest.target_pa, &ifc.ip)) { //check, if the reply was meant for us
            learn_arp(arpRequest.sender_pa, arpRequest.sender_ha, ifc);
        } else {
            fprintf(stderr, "Odd, received ARP reply that is not for me!\n");
        }
    }
}

/**
 * Process ARP (request or response!)
 *
 * @param ifc interface we received the ARP request from
 * @param eh ethernet header
 * @param ah ARP header
 */
static void
handle_arp (struct Interface *ifc,
            const struct EthernetHeader *eh,
            const struct ArpHeaderEthernetIPv4 *ah)
{
    struct Interface interface = *ifc;
    struct ArpHeaderEthernetIPv4 arp = *ah;
    if(1 == ntohs(ah->oper)) { //request
        handle_arp_request(arp, interface);
    }
    else if (2 ==ntohs(ah->oper)) { //response
        lookup_and_add_ARP(arp, interface);
    }
}

static void send_broadcast_APR(struct Interface *ifc, struct in_addr target_IP) {

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
    arpHeader.target_pa = target_IP;

    uint8_t sendBuff[sizeof(struct EthernetHeader) + sizeof(struct ArpHeaderEthernetIPv4)];
    memcpy(&sendBuff, &ethHeader, sizeof(ethHeader));
    memcpy(&sendBuff[sizeof ethHeader], &arpHeader, sizeof(arpHeader));
    forward_to(ifc, &sendBuff, sizeof(sendBuff));
}

int timer = 0;

/**
 * Parse and process frame received on @a ifc.
 *
 * @param ifc interface we got the frame on
 * @param frame raw frame data
 * @param frame_size number of bytes in @a frame
 */

static void
parse_frame (struct Interface *ifc,
             const void *frame,
             size_t frame_size)
{
    struct EthernetHeader eh;
    const char *cframe = frame;
    struct Interface interface = *ifc;


    if (0 == timer) { //only do it once!
        init_router(); //initialize values for routing table
        timer = 1;
    }

    if (frame_size < sizeof (eh))
    {
        fprintf (stderr,
                 "Malformed frame\n");
        return;
    }
    memcpy (&eh,
            frame,
            sizeof (eh));

    switch (ntohs (eh.tag))
    {
        case ETH_P_IPV4:

        {
            struct IPv4Header ip;


            if (frame_size < sizeof (struct EthernetHeader) + sizeof (struct IPv4Header))
            {
                fprintf (stderr,
                         "Malformed frame\n");
                return;
            }
            memcpy (&ip,
                    &cframe[sizeof (struct EthernetHeader)],
                    sizeof (struct IPv4Header));

            route (ip, ifc, &eh, &cframe[sizeof (struct EthernetHeader) + sizeof (struct IPv4Header)],
                   frame_size - sizeof (struct EthernetHeader) - sizeof (struct IPv4Header));


            break;
        }
        case ETH_P_ARP:

        {
            struct ArpHeaderEthernetIPv4 ah;

            if (frame_size < sizeof (struct EthernetHeader) + sizeof (struct
                    ArpHeaderEthernetIPv4))
            {
#if DEBUG
                fprintf (stderr,
                 "Unsupported ARP frame\n");
#endif
                return;
            }
            memcpy (&ah,
                    &cframe[sizeof (struct EthernetHeader)],
                    sizeof (struct ArpHeaderEthernetIPv4));


            handle_arp (ifc,
                        &eh,
                        &ah);
            break;
        }
        default:
#if DEBUG
            fprintf (stderr,
             "Unsupported Ethernet tag %04X\n",
             ntohs (eh.tag));
#endif
            return;
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
handle_frame (uint16_t interface,
              const void *frame,
              size_t frame_size)
{
    if (interface > num_ifc)
        abort ();
    parse_frame (&gifc[interface - 1],
                 frame,
                 frame_size);
}


/**
 * Find network interface by @a name.
 *
 * @param name name to look up by
 * @return NULL if @a name was not found
 */
static struct Interface *
find_interface (const char *name)
{
    for (unsigned int i = 0; i<num_ifc; i++)
        if (0 == strcasecmp (name,
                             gifc[i].name))
            return &gifc[i];
    return NULL;
}

// copy von arp.c // Mac
static void print_arp_cache(){
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

static int ipv4_lookup(struct in_addr ip4){
    for (int i = 0; i < arpCacheSize; i++) {
        if (0== ipcomp(&ip4, &arpCache[i].ip)) {
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
process_cmd_arp ()
{
    const char *tok = strtok (NULL, " ");
    struct in_addr v4;
    struct MacAddress mac;
    struct Interface *ifc;

    if (NULL == tok)
    {
        print_arp_cache();
        return;
    }
    if (1 !=
        inet_pton (AF_INET,
                   tok,
                   &v4))
    {
        fprintf (stderr,
                 "`%s' is not a valid IPv4 address\n",
                 tok);
        return;
    }
    tok = strtok (NULL, " ");
    if (NULL == tok)
    {
        fprintf (stderr,
                 "No network interface provided\n");
        return;
    }
    ifc = find_interface (tok);
    if (NULL == ifc)
    {
        fprintf (stderr,
                 "Interface `%s' unknown\n",
                 tok);
        return;
    }
    if (-1 == ipv4_lookup(v4)) {
        send_broadcast_APR(ifc, v4);
    }
}


/**
 * Parse network specification in @a net, initializing @a network and @a netmask.
 * Format of @a net is "IP/NETMASK".
 *
 * @param network[out] network specification to initialize
 * @param netmask[out] netmask specification to initialize
 * @param arg interface specification to parse
 * @return 0 on success
 */
static int
parse_network (struct in_addr *network,
               struct in_addr *netmask,
               const char *net)
{
    const char *tok;
    char *ip;
    unsigned int mask;

    tok = strchr (net, '/');
    if (NULL == tok)
    {
        fprintf (stderr,
                 "Error in network specification: lacks '/'\n");
        return 1;
    }
    ip = strndup (net,
                  tok - net);
    if (1 !=
        inet_pton (AF_INET,
                   ip,
                   network))
    {
        fprintf (stderr,
                 "IP address `%s' malformed\n",
                 ip);
        free (ip);
        return 1;
    }
    free (ip);
    tok++;
    if (1 !=
        sscanf (tok,
                "%u",
                &mask))
    {
        fprintf (stderr,
                 "Netmask `%s' malformed\n",
                 tok);
        return 1;
    }
    if (mask > 32)
    {
        fprintf (stderr,
                 "Netmask invalid (too large)\n");
        return 1;
    }
    netmask->s_addr = htonl (~(uint32_t) ((1LLU << (32 - mask)) - 1LLU));
    return 0;
}


/**
 * Parse route from arguments in strtok() buffer.
 *
 * @param target_network[out] set to target network
 * @param target_netmask[out] set to target netmask
 * @param next_hop[out] set to next hop
 * @param ifc[out] set to target interface
 */
static int
parse_route (struct in_addr *target_network,
             struct in_addr *target_netmask,
             struct in_addr *next_hop,
             struct Interface **ifc)
{
    char *tok;

    tok = strtok (NULL, " ");
    if ( (NULL == tok) ||
         (0 != parse_network (target_network,
                              target_netmask,
                              tok)) )
    {
        fprintf (stderr,
                 "Expected network specification, not `%s'\n",
                 tok);
        return 1;
    }
    tok = strtok (NULL, " ");
    if ( (NULL == tok) ||
         (0 != strcasecmp ("via",
                           tok)))
    {
        fprintf (stderr,
                 "Expected `via', not `%s'\n",
                 tok);
        return 1;
    }
    tok = strtok (NULL, " ");
    if ( (NULL == tok) ||
         (1 != inet_pton (AF_INET,
                          tok,
                          next_hop)) )
    {
        fprintf (stderr,
                 "Expected next hop, not `%s'\n",
                 tok);
        return 1;
    }
    tok = strtok (NULL, " ");
    if ( (NULL == tok) ||
         (0 != strcasecmp ("dev",
                           tok)))
    {
        fprintf (stderr,
                 "Expected `dev', not `%s'\n",
                 tok);
        return 1;
    }
    tok = strtok (NULL, " ");
    *ifc = find_interface (tok);
    if (NULL == *ifc)
    {
        fprintf (stderr,
                 "Interface `%s' unknown\n",
                 tok);
        return 1;
    }
    return 0;
}


/**
 * Add a route.
 */
static void
process_cmd_route_add ()
{
    struct in_addr target_network;
    struct in_addr target_netmask;
    struct in_addr next_hop;
    struct Interface *ifc;

    if (0 != parse_route (&target_network, &target_netmask, &next_hop, &ifc)) {
        return;
    } else {
        struct in_addr network_IP;
        struct Routing_entry new_entry;
        new_entry.network_target = target_network;
        new_entry.network_mask = target_netmask;
        new_entry.gateway = next_hop;
        new_entry.ifc = *ifc;
        struct MacAddress mac = lookup_ipv4_inARP(new_entry.gateway); //lookup Mac address of gateway in ARP table

        if (0 == maccomp(&NULL_ADDRESS, &mac)) { //if the mac address is NOT found in ARP-table, it can not be used for routing, abort
            return;
        }
        add_entry(&new_entry);
    }

}


/**
 * Delete a route.
 */
static void
process_cmd_route_del ()
{
    struct in_addr target_network;
    struct in_addr target_netmask;
    struct in_addr next_hop;
    struct Interface *ifc;

    if (0 != parse_route (&target_network, &target_netmask, &next_hop, &ifc)) {
        return;
    } else {
        struct Routing_entry* deletable_entry = lookup_rt_for_del(target_network, target_netmask, next_hop, ifc);
        if(NULL != deletable_entry) {
            delete_entry(deletable_entry);
        }
    }

}


/**
 * Print out the routing table.
 */
static void
process_cmd_route_list ()
{
    struct Routing_entry *iter;
   for (iter = routing_table; NULL != iter; iter = iter->next){

        print_ip(&iter->network_target);
        print("/");
        print_ip(&iter->network_mask);
        print(" -> ");
        print_ip(&iter->gateway);
        print(" (");
        print("%s", iter->ifc.name);
        print(")\n");

   }

 }


/**
 * The user entered a "route" command.  The remaining
 * arguments can be obtained via 'strtok()'.
 */
static void
process_cmd_route ()
{
    char *subcommand = strtok (NULL, " ");

    if (NULL == subcommand)
        subcommand = "list";
    if (0 == strcasecmp ("add",
                         subcommand))
        process_cmd_route_add ();
    else if (0 == strcasecmp ("del",
                              subcommand))
        process_cmd_route_del ();
    else if (0 == strcasecmp ("list",
                              subcommand))
        process_cmd_route_list ();
    else
        fprintf (stderr,
                 "Subcommand `%s' not understood\n",
                 subcommand);
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
parse_network_arg (struct Interface *ifc,
                   const char *net)
{

    if (0 !=
        strncasecmp (net,
                     "IPV4:",
                     strlen ("IPV4:")))
    {
        fprintf (stderr,
                 "Interface specification `%s' does not start with `IPV4:'\n",
                 net);
        return 1;
    }
    net += strlen ("IPV4:");
    return parse_network (&ifc->ip,
                          &ifc->netmask,
                          net);
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
parse_cmd_arg (struct Interface *ifc,
               const char *arg)
{
    const char *tok;
    char *nspec;

    ifc->mtu = 1500 + sizeof (struct EthernetHeader); /* default in case unspecified */
    tok = strchr (arg, '[');
    if (NULL == tok)
    {
        fprintf (stderr,
                 "Error in interface specification: lacks '['");
        return 1;
    }
    ifc->name = strndup (arg,
                         tok - arg);
    arg = tok + 1;
    tok = strchr (arg, ']');
    if (NULL == tok)
    {
        fprintf (stderr,
                 "Error in interface specification: lacks ']'");
        return 1;
    }
    nspec = strndup (arg,
                     tok - arg);
    if (0 !=
        parse_network_arg (ifc,
                           nspec))
    {
        free (nspec);
        return 1;
    }
    free (nspec);
    arg = tok + 1;
    if ('=' == arg[0])
    {
        unsigned int mtu;

        if (1 != (sscanf (&arg[1],
                          "%u",
                          &mtu)))
        {
            fprintf (stderr,
                     "Error in interface specification: MTU not a number\n");
            return 1;
        }
        if (mtu < 400)
        {
            fprintf (stderr,
                     "Error in interface specification: MTU too small\n");
            return 1;
        }
        if (mtu > UINT16_MAX)
        {
            fprintf (stderr,
                     "Error in interface specification: MTU too large\n");
            return 1;
        }
        ifc->mtu = mtu + sizeof (struct EthernetHeader);
#if DEBUG
        fprintf (stderr,
             "Interface %s has MTU %u\n",
             ifc->name,
             (unsigned int) ifc->mtu);
#endif
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
handle_control (char *cmd,
                size_t cmd_len)
{
    const char *tok;

    cmd[cmd_len - 1] = '\0';
    tok = strtok (cmd,
                  " ");
    if (NULL == tok)
        return;
    if (0 == strcasecmp (tok,
                         "arp"))
        process_cmd_arp ();
    else if (0 == strcasecmp (tok,
                              "route"))
        process_cmd_route ();
    else
        fprintf (stderr,
                 "Unsupported command `%s'\n",
                 tok);
}


/**
 * Handle MAC information @a mac
 *
 * @param ifc_num number of the interface with @a mac
 * @param mac the MAC address at @a ifc_num
 */
static void
handle_mac (uint16_t ifc_num,
            const struct MacAddress *mac)
{
    if (ifc_num > num_ifc)
        abort ();
    gifc[ifc_num - 1].mac = *mac;
}


#include "loop.c"


/**
 * Launches the router.
 *
 * @param argc number of arguments in @a argv
 * @param argv binary name, followed by list of interfaces to switch between
 * @return not really
 */
int
main (int argc,
      char **argv)
{
    struct Interface ifc[argc];

    memset (ifc,
            0,
            sizeof (ifc));
    num_ifc = argc - 1;
    gifc = ifc;


    for (unsigned int i = 1; i<argc; i++)
    {
        struct Interface *p = &ifc[i - 1];

        ifc[i - 1].ifc_num = i;
        if (0 !=
            parse_cmd_arg (p,
                           argv[i]))
            abort ();
    }


    inet_pton(AF_INET, "0.0.0.0", &IP0);
    loop ();
    for (unsigned int i = 1; i<argc; i++)
        free (ifc[i - 1].name);
    return 0;
}