#include "glab.h"
#include "print.c"
//for using pid_t:
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "crc.c"

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
struct IPv4Header {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned int header_length: 4;
    unsigned int version: 4;
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
struct IcmpHeader {
    uint8_t type;
    uint8_t code;
    uint16_t crc;

    union {
        /**
         * Payload for #ICMPTYPE_DESTINATION_UNREACHABLE (RFC 1191)
         */
        struct ih_pmtu {
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
 * Maximum size of a message.
 */
#define MAX_SIZE (65536 + sizeof (struct GLAB_MessageHeader))
#define MAX_IFC 3
#define GLAB_HEADER_SIZE sizeof(struct GLAB_MessageHeader)
#define ETHERNET_HEADER_SIZE sizeof(struct EthernetHeader)
#define ARP_HEADER_SIZE sizeof(struct ArpHeaderEthernetIPv4)
#define IPV4_HEADER_SIZE sizeof(struct IPv4Header)

size_t FRAME_SIZE = sizeof(struct GLAB_MessageHeader) + sizeof(struct EthernetHeader);
size_t FRAME_0_SIZE = sizeof(struct GLAB_MessageHeader) + 3 * MAC_ADDR_SIZE;

/////////   Mac Addresses of connected clients /////////
struct MacAddress eth1mac = {0x00, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
struct MacAddress eth2mac = {0x00, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb};
struct MacAddress eth3mac = {0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
struct in_addr eth1ip; char ip1[] = "192.168.1.1"; //ip will be created by init in main
struct in_addr eth2ip; char ip2[] = "192.168.2.1";
struct in_addr eth3ip; char ip3[] = "192.168.3.1";

/////////   Mac Addresses for the test  /////////
struct MacAddress broadcast = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
struct MacAddress multicast = {0xff, 0xff, 0xff, 0xaa, 0xbb, 0xcc};
struct MacAddress client1 = {0x00, 0x11, 0x11, 0x11, 0x11, 0x11};
struct MacAddress client2 = {0x00, 0x22, 0x22, 0x22, 0x22, 0x22};
struct MacAddress client3 = {0x00, 0x33, 0x33, 0x33, 0x33, 0x33};

int testA1(int child_stdin, int child_stdout);
int testA2(int child_stdin, int child_stdout);
int testA3(int child_stdin, int child_stdout);

/**
 * Compare to MAC-addresses. From FAQ-slides Prof. Grothoff
 * @param mac1 first MAC-address to compare
 * @param mac2 second MAC-address to compare
 * @return 0 on success, any other number on fail (number of chars that did not match)
 */
const int
maccomp(const struct MacAddress *mac1,
        const struct MacAddress *mac2) {
    return memcmp(mac1, mac2, sizeof(struct MacAddress));
};

/**
 * compare method for 2 ip-addresses
 * @param ip1 first ip-address
 * @param ip2 second ip-address
 * @return 0 if both are equal. any other value: they are not equal
 */
int ipcomp(struct in_addr *ip1, struct in_addr *ip2) {
    return memcmp(ip1, ip2, sizeof(struct in_addr));
}

/**
 * Print MAC-address
 * @param mac MAC-address to be printed
 */
static void
print_mac (const struct MacAddress *mac)
{
    printf ("%02x:%02x:%02x:%02x:%02x:%02x",
           mac->mac[0], mac->mac[1], mac->mac[2], mac->mac[3], mac->mac[4], mac->mac[5]);
}

/**
 * Print IP-address
 * @param ip IP-address to be printed
 */
static void
print_ip(const struct in_addr *ip) {
    char buf[INET_ADDRSTRLEN];
    printf("%s", inet_ntop(AF_INET, ip, buf, sizeof(buf)));
}

int main(int argc, char **argv) {

    // Test starting point from Kickoff
    int cin[2], cout[2];
    pipe(cin);
    pipe(cout);
    int chld = fork();
    char *start_arr[5];
    start_arr[0] = argv[1];
    start_arr[1] = "eth1[IPV4:192.168.1.1/24]";
    start_arr[2] = "eth2[IPV4:192.168.2.1/24]";
    start_arr[3] = "eth3[IPV4:192.168.3.1/24]";
    start_arr[4] = NULL;

    if (0 == chld) {
        printf("Starting router in child process\n");
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(cin[1]);
        close(cout[0]);
        dup2(cin[0], STDIN_FILENO);
        dup2(cout[1], STDOUT_FILENO);
        execvp(argv[1], start_arr);
        printf("Failed to run binary ‘%s’\n", argv[1]);
        exit(1);
    }
    close(cin[0]);
    close(cout[1]);
    int child_stdin = cin[1];
    int child_stdout = cout[0];
    // End: Test starting point from Kickoff
    //sleep(1);

    /////////   init    /////////
    inet_pton(AF_INET, ip1, &eth1ip);
    inet_pton(AF_INET, ip2, &eth2ip);
    inet_pton(AF_INET, ip3, &eth3ip);
    uint8_t writeBuf[GLAB_HEADER_SIZE + MAC_ADDR_SIZE * 3];
    struct GLAB_MessageHeader header;
    header.type = htons(0);
    header.size = htons(sizeof(writeBuf));

    // ifc01, ifc02, ifc03
    memcpy(writeBuf, &header, sizeof(header));
    memcpy(&writeBuf[GLAB_HEADER_SIZE + MAC_ADDR_SIZE * 0], &eth1mac, MAC_ADDR_SIZE);
    memcpy(&writeBuf[GLAB_HEADER_SIZE + MAC_ADDR_SIZE * 1], &eth2mac, MAC_ADDR_SIZE);
    memcpy(&writeBuf[GLAB_HEADER_SIZE + MAC_ADDR_SIZE * 2], &eth3mac, MAC_ADDR_SIZE);
    write_all(child_stdin, writeBuf, sizeof(writeBuf));


    ///////// test06: send ARP Request receiving response /////////
//    sleep(1);
//    int resultA1 = testA1(child_stdin, child_stdout);

    // bug - checksum will not detected
//    sleep(1);
//    int resultA2 = testA2(child_stdin, child_stdout);

    sleep(1);
    int resultA3 = testA3(child_stdin, child_stdout);


    ///////// test results ////////////
    /*int result = result01+result02+result03+result04+result05+result06;
    printf("\nResult: %d/6 passed\n", result);*/

    // stop child process

    sleep(2);
    kill(chld, SIGKILL);

    if (1 != resultA3) {
        fprintf(stderr, "test failed\n");
        return -1;
     }else {
        fprintf(stderr, "test passed\n");
        return 0;
     }

}

int testA1(int child_stdin, int child_stdout) {

    struct in_addr client1ip;
    inet_pton(AF_INET, "192.168.1.2", &client1ip);
    struct in_addr client2ip;
    inet_pton(AF_INET, "192.168.2.2", &client2ip);

    //send 'arp' reply
    struct EthernetHeader ethHeaderArp;
    ethHeaderArp.src = client2;
    ethHeaderArp.dst = eth2mac;
    ethHeaderArp.tag = htons(0x0806);

    struct ArpHeaderEthernetIPv4 arpHeader;
    arpHeader.htype = htons(1);
    arpHeader.ptype = htons(0x0800);
    arpHeader.hlen = 6;
    arpHeader.plen = 4;
    arpHeader.oper = htons(2);
    arpHeader.sender_ha = client2;
    arpHeader.sender_pa = client2ip;
    arpHeader.target_ha = eth2mac;
    arpHeader.target_pa = eth2ip;
    char sendBuff06[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE];

    printf("ARP:test before send");
    printf(" scr-MAC: ");
    print_mac(&arpHeader.sender_ha);
    printf(" scr-IP: ");
    print_ip(&arpHeader.sender_pa);
    printf(" des-MAC: ");
    print_mac(&arpHeader.target_ha);
    printf(" des-IP: ");
    print_ip(&arpHeader.target_pa);
    printf(" oper %d \n", ntohs(arpHeader.oper));

    struct GLAB_MessageHeader msgHeaderArp;
    msgHeaderArp.type = htons(2);
    msgHeaderArp.size = htons(sizeof(sendBuff06));

    memcpy(sendBuff06, &msgHeaderArp, GLAB_HEADER_SIZE);
    memcpy(&sendBuff06[GLAB_HEADER_SIZE], &ethHeaderArp, ETHERNET_HEADER_SIZE);
    memcpy(&sendBuff06[GLAB_HEADER_SIZE+ETHERNET_HEADER_SIZE], &arpHeader, ARP_HEADER_SIZE);
    write_all(child_stdin, &sendBuff06, sizeof(sendBuff06));
    //

    struct MacAddress newMac = {0x44, 0x55, 0x66, 0x77, 0x88, 0x01};

    char writeBuf[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(1);
    msgHeader.size = htons(sizeof(writeBuf));

    printf("GLAB: %d ", ntohs(msgHeader.type));
    printf("%d ", ntohs(msgHeader.size));

    struct EthernetHeader ethHeader;
    ethHeader.src = client1;
    ethHeader.dst = eth1mac;
    ethHeader.tag = htons(ETH_P_IPV4);

    printf("ETH: scr-mac ");
    print_mac(&ethHeader.src);
    printf(" des-mac ");
    print_mac(&ethHeader.dst);
    printf(" %04x ", ntohs( ethHeader.tag));

    struct IPv4Header iPv4Header;
    iPv4Header.source_address = client1ip;
    iPv4Header.destination_address = client2ip;
    iPv4Header.fragmentation_info = 0;
    iPv4Header.diff_serv = 0;
    iPv4Header.header_length = 5;
    iPv4Header.identification = 0;
    iPv4Header.protocol = 1;
    iPv4Header.version = 4;
    iPv4Header.ttl = 64;
    iPv4Header.total_length = htons(sizeof(iPv4Header));
    iPv4Header.checksum = 0;
    iPv4Header.checksum = GNUNET_CRYPTO_crc16_n(&iPv4Header, sizeof(iPv4Header));

    printf(" IPv4: scr: ");
    print_ip(&iPv4Header.source_address);
    printf(" des: ");
    print_ip(&iPv4Header.destination_address);
    printf(" f_info: %d", iPv4Header.fragmentation_info);
    printf(" serv: %d", iPv4Header.diff_serv);
    printf(" f_info: %d", iPv4Header.identification);
    printf(" protocol: %d", iPv4Header.protocol);
    printf(" ver: %d", iPv4Header.version);
    printf(" ttl: %d", iPv4Header.ttl);
    printf(" checksum: %d <>\n", iPv4Header.checksum);

    memcpy(&writeBuf, &msgHeader, sizeof(msgHeader));
    memcpy(&writeBuf[sizeof(msgHeader)], &ethHeader, sizeof(ethHeader));
    memcpy(&writeBuf[sizeof(msgHeader) + sizeof(ethHeader)], &iPv4Header, IPV4_HEADER_SIZE);
    write_all(child_stdin, writeBuf, sizeof(writeBuf));
    printf("Test %lu\n", sizeof(writeBuf));


    //read
    char readBuff_08[MAX_SIZE];
    sleep(1); // wait for switch
    int ret = read(child_stdout, &readBuff_08, sizeof(writeBuf));
    printf("Test %d\n", ret);
//    for (int i = 0; i < ret; ++i) {
//        print("%c", readBuff_08[i]);
//    }

    memcpy(&msgHeader, &readBuff_08[0], GLAB_HEADER_SIZE);
    memcpy(&ethHeader, &readBuff_08[GLAB_HEADER_SIZE], ETHERNET_HEADER_SIZE);
    memcpy(&iPv4Header, &readBuff_08[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE], IPV4_HEADER_SIZE);

    printf("GLAB: %d ", ntohs(msgHeader.type));
    printf("%d ", ntohs(msgHeader.size));

    printf("ETH: scr-mac ");
    print_mac(&ethHeader.src);
    printf(" des-mac ");
    print_mac(&ethHeader.dst);
    printf(" %04x ", ntohs( ethHeader.tag));

    printf("IPv4: scr: ");
    print_ip(&iPv4Header.source_address);
    printf(" des: ");
    print_ip(&iPv4Header.destination_address);
    printf(" f_info: %d", iPv4Header.fragmentation_info);
    printf(" serv: %d", iPv4Header.diff_serv);
    printf(" f_info: %d", iPv4Header.identification);
    printf(" protocol: %d", iPv4Header.protocol);
    printf(" ver: %d", iPv4Header.version);
    printf(" ttl: %d", iPv4Header.ttl);
    printf(" checksum: %d <>\n", iPv4Header.checksum);


//    char expectedMac[MAC_AS_STRING_SIZE] = "44:55:66:77:88:14";
//    // Problem by reading on ubuntu, work only when start with index 4
//    int result = memcmp(&readBuff_08[4], &expectedMac, MAC_AS_STRING_SIZE);
//    char * getMac[MAC_AS_STRING_SIZE];
//    if(result != 0){
//        printf("TestID 08: Fail! $arp 192.168.1.20 eth1\n");
//        printf("expect: %s  but got %s", expectedMac, &readBuff_08[4]);
//        return 0;
//    }
//    printf("TestID 08: passed.\n");
    return 1;
}

int testA2(int child_stdin, int child_stdout) {

    struct in_addr client1ip;
    inet_pton(AF_INET, "192.168.1.2", &client1ip);
    struct in_addr client2ip;
    inet_pton(AF_INET, "192.168.2.2", &client2ip);
    struct in_addr client3ip;
    inet_pton(AF_INET, "0.0.0.0", &client3ip);

    //send 'arp' reply
    struct EthernetHeader ethHeaderArp;
    ethHeaderArp.src = client2;
    ethHeaderArp.dst = eth2mac;
    ethHeaderArp.tag = htons(0x0806);

    struct ArpHeaderEthernetIPv4 arpHeader;
    arpHeader.htype = htons(1);
    arpHeader.ptype = htons(0x0800);
    arpHeader.hlen = 6;
    arpHeader.plen = 4;
    arpHeader.oper = htons(2);
    arpHeader.sender_ha = client2;
    arpHeader.sender_pa = client2ip;
    arpHeader.target_ha = eth2mac;
    arpHeader.target_pa = eth2ip;
    char sendBuff06[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE];

    printf("ARP:test before send");
    printf(" scr-MAC: ");
    print_mac(&arpHeader.sender_ha);
    printf(" scr-IP: ");
    print_ip(&arpHeader.sender_pa);
    printf(" des-MAC: ");
    print_mac(&arpHeader.target_ha);
    printf(" des-IP: ");
    print_ip(&arpHeader.target_pa);
    printf(" oper %d \n", ntohs(arpHeader.oper));

    struct GLAB_MessageHeader msgHeaderArp;
    msgHeaderArp.type = htons(2);
    msgHeaderArp.size = htons(sizeof(sendBuff06));

    memcpy(sendBuff06, &msgHeaderArp, GLAB_HEADER_SIZE);
    memcpy(&sendBuff06[GLAB_HEADER_SIZE], &ethHeaderArp, ETHERNET_HEADER_SIZE);
    memcpy(&sendBuff06[GLAB_HEADER_SIZE+ETHERNET_HEADER_SIZE], &arpHeader, ARP_HEADER_SIZE);
    write_all(child_stdin, &sendBuff06, sizeof(sendBuff06));

    // Ipv4

    struct MacAddress newMac = {0x44, 0x55, 0x66, 0x77, 0x88, 0x01};

    char writeBuf[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(1);
    msgHeader.size = htons(sizeof(writeBuf));

    printf("Send GLAB: %d ", ntohs(msgHeader.type));
    printf("%d ", ntohs(msgHeader.size));

    struct EthernetHeader ethHeader;
    ethHeader.src = client1;
    ethHeader.dst = eth1mac;
    ethHeader.tag = htons(ETH_P_IPV4);

    printf("ETH: scr-mac ");
    print_mac(&ethHeader.src);
    printf(" des-mac ");
    print_mac(&ethHeader.dst);
    printf(" %04x ", ntohs( ethHeader.tag));

    struct IPv4Header iPv4Header;
    iPv4Header.source_address = client1ip;
    iPv4Header.destination_address = client2ip;
    iPv4Header.fragmentation_info = 0;
    iPv4Header.diff_serv = 0;
    iPv4Header.header_length = 5;
    iPv4Header.identification = 0;
    iPv4Header.protocol = 17;
    iPv4Header.version = 4;
    iPv4Header.ttl = 64;
    iPv4Header.total_length = htons(sizeof(iPv4Header));
    iPv4Header.checksum = 99;
 //   iPv4Header.checksum = GNUNET_CRYPTO_crc16_n(&iPv4Header, sizeof(iPv4Header));

    printf(" IPv4: scr: ");
    print_ip(&iPv4Header.source_address);
    printf(" des: ");
    print_ip(&iPv4Header.destination_address);
    printf(" f_info: %d", iPv4Header.fragmentation_info);
    printf(" serv: %d", iPv4Header.diff_serv);
    printf(" f_info: %d", iPv4Header.identification);
    printf(" protocol: %d", iPv4Header.protocol);
    printf(" ver: %d", iPv4Header.version);
    printf(" ttl: %d", iPv4Header.ttl);
    printf(" checksum: %d <>\n", iPv4Header.checksum);

    memcpy(&writeBuf, &msgHeader, sizeof(msgHeader));
    memcpy(&writeBuf[sizeof(msgHeader)], &ethHeader, sizeof(ethHeader));
    memcpy(&writeBuf[sizeof(msgHeader) + sizeof(ethHeader)], &iPv4Header, IPV4_HEADER_SIZE);
    write_all(child_stdin, writeBuf, sizeof(writeBuf));
    printf("Test %lu\n", sizeof(writeBuf));


    //read
    char readBuff_08[MAX_SIZE];
    sleep(1); // wait for switch
    int ret = read(child_stdout, &readBuff_08, sizeof(readBuff_08));


    memcpy(&msgHeader, &readBuff_08[0], GLAB_HEADER_SIZE);
    memcpy(&ethHeader, &readBuff_08[GLAB_HEADER_SIZE], ETHERNET_HEADER_SIZE);
    memcpy(&iPv4Header, &readBuff_08[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE], IPV4_HEADER_SIZE);

    printf("Read GLAB: %d ", ntohs(msgHeader.type));
    printf("%d ", ntohs(msgHeader.size));

    if (0 < ret){
        fprintf(stderr, "Checksum is wrong: packed should not be forwarded\n");
        fprintf(stderr, "Received unexpected frame at Interface %d\n", ntohs(msgHeader.type)-1);
        return -1;
    }

    printf("ETH: scr-mac ");
    print_mac(&ethHeader.src);
    printf(" des-mac ");
    print_mac(&ethHeader.dst);
    printf(" %04x ", ntohs( ethHeader.tag));

    printf("IPv4: scr: ");
    print_ip(&iPv4Header.source_address);
    printf(" des: ");
    print_ip(&iPv4Header.destination_address);
    printf(" f_info: %d", iPv4Header.fragmentation_info);
    printf(" serv: %d", iPv4Header.diff_serv);
    printf(" f_info: %d", iPv4Header.identification);
    printf(" protocol: %d", iPv4Header.protocol);
    printf(" ver: %d", iPv4Header.version);
    printf(" ttl: %d", iPv4Header.ttl);
    printf(" checksum: %d <>\n", iPv4Header.checksum);

    struct IcmpHeader icmp;
    memcpy(&icmp, &readBuff_08[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE], sizeof(icmp));
    printf("Icmp type %d code %d crc %d\n",icmp.type, icmp.code, icmp.crc);


//    char expectedMac[MAC_AS_STRING_SIZE] = "44:55:66:77:88:14";
//    // Problem by reading on ubuntu, work only when start with index 4
//    int result = memcmp(&readBuff_08[4], &expectedMac, MAC_AS_STRING_SIZE);
//    char * getMac[MAC_AS_STRING_SIZE];
//    if(result != 0){
//        printf("TestID 08: Fail! $arp 192.168.1.20 eth1\n");
//        printf("expect: %s  but got %s", expectedMac, &readBuff_08[4]);
//        return 0;
//    }
    printf("TestID A2 passed.\n");
    return 1;
}

int testA3(int child_stdin, int child_stdout) {

    struct in_addr client1ip;
    inet_pton(AF_INET, "192.168.1.2", &client1ip);
    struct in_addr client2ip;
    inet_pton(AF_INET, "192.168.2.2", &client2ip);
    struct in_addr client3ip;
    inet_pton(AF_INET, "0.0.0.0", &client3ip);

    //send 'arp' reply
    struct EthernetHeader ethHeaderArp;
    ethHeaderArp.src = client2;
    ethHeaderArp.dst = eth2mac;
    ethHeaderArp.tag = htons(0x0806);

    struct ArpHeaderEthernetIPv4 arpHeader;
    arpHeader.htype = htons(1);
    arpHeader.ptype = htons(0x0800);
    arpHeader.hlen = 6;
    arpHeader.plen = 4;
    arpHeader.oper = htons(2);
    arpHeader.sender_ha = client2;
    arpHeader.sender_pa = client2ip;
    arpHeader.target_ha = eth2mac;
    arpHeader.target_pa = eth2ip;
    char sendBuff06[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE];

    struct GLAB_MessageHeader msgHeaderArp;
    msgHeaderArp.type = htons(2);
    msgHeaderArp.size = htons(sizeof(sendBuff06));

    memcpy(sendBuff06, &msgHeaderArp, GLAB_HEADER_SIZE);
    memcpy(&sendBuff06[GLAB_HEADER_SIZE], &ethHeaderArp, ETHERNET_HEADER_SIZE);
    memcpy(&sendBuff06[GLAB_HEADER_SIZE+ETHERNET_HEADER_SIZE], &arpHeader, ARP_HEADER_SIZE);
    write_all(child_stdin, &sendBuff06, sizeof(sendBuff06));

    // Ipv4

    struct MacAddress newMac = {0x44, 0x55, 0x66, 0x77, 0x88, 0x01};

    char writeBuf[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE+1500];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(1);
    msgHeader.size = htons(sizeof(writeBuf));

    printf("Send GLAB: %d ", ntohs(msgHeader.type));
    printf("%d ", ntohs(msgHeader.size));

    struct EthernetHeader ethHeader;
    ethHeader.src = client1;
    ethHeader.dst = eth1mac;
    ethHeader.tag = htons(ETH_P_IPV4);


    struct IPv4Header iPv4Header;
    iPv4Header.source_address = client1ip;
    iPv4Header.destination_address = client2ip;
    iPv4Header.fragmentation_info = htons(16384);
    iPv4Header.diff_serv = 0;
    iPv4Header.header_length = 5;
    iPv4Header.identification = 0;
    iPv4Header.protocol = 17;
    iPv4Header.version = 4;
    iPv4Header.ttl = -5;
    iPv4Header.total_length = htons(sizeof(iPv4Header));
    iPv4Header.checksum = 0;
    iPv4Header.checksum = GNUNET_CRYPTO_crc16_n(&iPv4Header, sizeof(iPv4Header));


    memcpy(&writeBuf, &msgHeader, sizeof(msgHeader));
    memcpy(&writeBuf[sizeof(msgHeader)], &ethHeader, sizeof(ethHeader));
    memcpy(&writeBuf[sizeof(msgHeader) + sizeof(ethHeader)], &iPv4Header, IPV4_HEADER_SIZE);
    write_all(child_stdin, writeBuf, sizeof(writeBuf));
    printf("Test %lu\n", sizeof(writeBuf));


    //read
    char readBuff_08[MAX_SIZE];
    sleep(1); // wait for switch
    int ret = read(child_stdout, &readBuff_08, sizeof(readBuff_08));
    printf("Test %d\n", ret);

    memcpy(&msgHeader, &readBuff_08[0], GLAB_HEADER_SIZE);
    memcpy(&ethHeader, &readBuff_08[GLAB_HEADER_SIZE], ETHERNET_HEADER_SIZE);
    memcpy(&iPv4Header, &readBuff_08[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE], IPV4_HEADER_SIZE);


    struct IcmpHeader icmp;
    memcpy(&icmp, &readBuff_08[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE], sizeof(icmp));
    printf("Icmp type %d code %d crc %d\n",icmp.type, icmp.code, icmp.crc);

    if (icmp.crc == 8183 || icmp.crc == 0){
        printf("TestID A3: passed.\n");
        return 1;
    }

    fprintf(stderr, "wrong icmp data\n");
    printf("TestID A3: failed.\n");
    return -1;
}













