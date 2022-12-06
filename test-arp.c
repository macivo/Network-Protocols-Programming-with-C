#include "glab.h"
#include "print.c"
//for using pid_t:
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>


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
 * Maximum size of a message.
 */
#define MAX_SIZE (65536 + sizeof (struct GLAB_MessageHeader))
#define MAX_IFC 3
#define GLAB_HEADER_SIZE sizeof(struct GLAB_MessageHeader)
#define ETHERNET_HEADER_SIZE sizeof(struct EthernetHeader)
#define ARP_HEADER_SIZE sizeof(struct ArpHeaderEthernetIPv4)


size_t FRAME_SIZE = sizeof(struct GLAB_MessageHeader)+sizeof(struct EthernetHeader);
size_t FRAME_0_SIZE = sizeof(struct GLAB_MessageHeader)+3*MAC_ADDR_SIZE;

/////////   Mac Addresses of connected clients /////////
struct MacAddress eth1mac = {0x00, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
struct MacAddress eth2mac = {0x00, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
struct MacAddress eth3mac = {0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
struct in_addr eth1ip; char ip1[] = "192.168.1.1"; //ip will be created by init in main
struct in_addr eth2ip; char ip2[] = "192.168.1.2";
struct in_addr eth3ip; char ip3[] = "192.168.1.3";

/////////   Mac Addresses for the test  /////////
struct MacAddress broadcast = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
struct MacAddress multicast = {0xFF, 0xFF, 0xFF, 0xAA, 0xBB, 0xCC};
struct MacAddress client1 = {0x00, 0x11, 0x11, 0x11, 0x11, 0x11};
struct MacAddress client2 = {0x00, 0x22, 0x22, 0x22, 0x22, 0x22};
struct MacAddress client3 = {0x00, 0x33, 0x33, 0x33, 0x33, 0x33};

/**
 * Compare to MAC-addresses. From FAQ-slides Prof. Grothoff
 * @param mac1 first MAC-address to compare
 * @param mac2 second MAC-address to compare
 * @return 0 on success, any other number on fail (number of chars that did not match)
 */
const int
maccomp (const struct MacAddress *mac1,
         const struct MacAddress *mac2) {
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

/**
 * Print MAC-address
 * @param mac MAC-address to be printed
 */
void print_mac (struct MacAddress mac){
    printf("Mac: %02X:%02X:%02X:%02X:%02X:%02X",
           mac.mac[0], mac.mac[1], mac.mac[2], mac.mac[3], mac.mac[4], mac.mac[5]);
};

/**
 * Print IP-address
 * @param ip IP-address to be printed
 */
static void
print_ip (const struct in_addr *ip)
{
    char buf[INET_ADDRSTRLEN];
    printf("%s", inet_ntop(AF_INET, ip, buf, sizeof (buf)));
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
    start_arr[2] = "eth2[IPV4:192.168.1.2/24]";
    start_arr[3] = "eth3[IPV4:192.168.1.3/24]";
    start_arr[4] = NULL;

    if (0 == chld) {
        printf("Starting arp in child process\n");
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
    sleep(1);
    int result06 = test06(child_stdin, child_stdout);

    /////////   Test08: Load testing with 100 ARP-replies  /////////
    sleep(1);
    int result08 = test08(child_stdin, child_stdout);

    sleep(1);


    ///////// test results ////////////
    /*int result = result01+result02+result03+result04+result05+result06;
    printf("\nResult: %d/6 passed\n", result);*/


    // stop child process
    kill(chld, SIGKILL);
    printf("Test procedure complete!\n");

    /* if (6 == result) {
         return 0;
     }else {
         return -1;
     }*/

    return 0;

}

//test

/**
 * TEST06
 * Send ARP-query to device in same network and receive ARP-reply.
 * @param child_stdin
 * @param child_stdout
 * @return 0 if test was successful otherwise 1
 */
int test06(int child_stdin, int child_stdout){

    printf("TestID 06: Send ARP-query to device in same network and receive ARP-reply\n");

    struct MacAddress unknownMac = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    struct MacAddress senderMac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

    struct in_addr known_IP;
    inet_pton (AF_INET, "192.168.1.1", &known_IP);
    struct in_addr sender_IP;
    inet_pton (AF_INET, "192.168.1.3", &sender_IP);

    struct EthernetHeader ethHeader;
    ethHeader.src = senderMac;
    ethHeader.dst = broadcast;
    ethHeader.tag = htons(0x0806);

    struct ArpHeaderEthernetIPv4 arpHeader;
    arpHeader.htype = htons(1);
    arpHeader.ptype = htons(0x0800);
    arpHeader.hlen = 6;
    arpHeader.plen = 4;
    arpHeader.oper = htons(1);
    arpHeader.sender_ha = senderMac;
    arpHeader.sender_pa = sender_IP;
    arpHeader.target_ha = unknownMac;
    arpHeader.target_pa = eth1ip;

    char sendBuff06[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE];

    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(1);
    msgHeader.size = htons(sizeof(sendBuff06));

    memcpy(sendBuff06, &msgHeader, GLAB_HEADER_SIZE);
    memcpy(&sendBuff06[GLAB_HEADER_SIZE], &ethHeader, ETHERNET_HEADER_SIZE);
    memcpy(&sendBuff06[GLAB_HEADER_SIZE+ETHERNET_HEADER_SIZE], &arpHeader, ARP_HEADER_SIZE);


    write_all(child_stdin, &sendBuff06, sizeof(sendBuff06));


    sleep(1);
    ssize_t ret;
    char readBuff06[MAX_SIZE];
    //printf("\nafter sleep1\n");
    ret = read(child_stdout, &readBuff06, MAX_SIZE);
    //printf("RET: %d", ret);
    sleep(1);


    //show send buffer

    struct GLAB_MessageHeader mh_test06;
    memcpy(&mh_test06, &readBuff06[0], GLAB_HEADER_SIZE);
    struct EthernetHeader eh_test06;
    memcpy(&eh_test06, &readBuff06[GLAB_HEADER_SIZE], ETHERNET_HEADER_SIZE);
    //printf("\nEH dst ");
    //print_mac(eh_test06.dst);
    //printf("\nEH src ");
    //print_mac(eh_test06.src);

    //check if sended data is right
    struct ArpHeaderEthernetIPv4 ah_test06;
    memcpy(&ah_test06, &readBuff06[ETHERNET_HEADER_SIZE+GLAB_HEADER_SIZE], sizeof(ah_test06));
    //printf("\nip address sender:");
    //print_ip(&ah_test06.sender_pa);
    //printf("\nip address receiver:");
    //print_ip(&ah_test06.target_pa);

    //printf("\n ENDBUFF NICOLE\n");

    //check if mac address sender request is mac address target reply
    //resonse 0 on succes
        int checkMac = maccomp(&senderMac, &eh_test06.dst);
        //printf("\ncheckMac request sender MAc == reply dest MAC: %d", checkMac );

    //check if mac address sender reply is not unknownMac {00:00:00:00:00:00}
    //resonse -1 on succes
    int checkMac_not_null;
    if (0 != maccomp(&unknownMac, &eh_test06.src)) {
        checkMac_not_null=0;
    };
    //printf("\ncheckMac_not_null request sender MAc is not {00:00:00:00:00:00}: %d", checkMac_not_null );
    //Soll nicht null sein!!!


    //check if ip address sender request is ip address target rreply
    //resonse 0 on succes
    int checkIPsender =  ipcomp (&sender_IP, &ah_test06.target_pa);
    //printf("\ncheckIPsender: request sender IP == reply target IP: %d", checkIPsender );
    //check if ip address receiver request is ip address sender reply
    //resonse 0 on succes
    int checkIPreceiver =  ipcomp (&eth1ip, &ah_test06.sender_pa);
    //printf("\ncheckIPreceiver: request receiver IP == reply sender IP: %d\n", checkIPreceiver );



if (0 == (checkMac + checkMac_not_null + checkIPsender + checkIPreceiver)){
    printf("TestID 06: succeeded\n");
    return 1;
} else {
    printf("TestID 06: failed\n");
    return 0;
}



}

/**
 * **** Test 07 *****
 * Send an ARP-reply not meant for the tested device (different IP)
 * @param child_stdin standard input number of device
 * @param child_stdout standard output number of device
 * @return 1 on success, 0 on failure
 */
 /*
int test07(int child_stdin, int child_stdout){
    printf("TestID 07: Send an ARP-reply not meant for the tested device\n");

    // send with ip 192.168.1.10-20
#define IP_BEGIN 10
#define IP_PREFIX_24 10 // length of string "192.168.1." /24 Netmask

    struct MacAddress newMac = {0x00, 0x99, 0x88, 0x77, 0x66, 0x55};


    int length = IP_PREFIX_24 + snprintf( NULL, 0, "%d" );
    char endIP07[length]; //IP as string
    sprintf(endIP07, "%s", "192.168.20.10");
    sprintf(&endIP07[IP_PREFIX_24], "%d", IP_BEGIN);
    struct in_addr newIP07;
    inet_pton(AF_INET, endIP07, &newIP07);

    char sender_IP07[length]; //IP as string
    sprintf(sender_IP07, "%s", "192.168.20.20");
    sprintf(&sender_IP07[IP_PREFIX_24], "%d", IP_BEGIN);
    struct in_addr senderIP07;
    inet_pton(AF_INET, sender_IP07, &senderIP07);

    char writeBuf07[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE];
    struct GLAB_MessageHeader msgHeader07;
    msgHeader07.type = htons(1);
    msgHeader07.size = htons(sizeof(writeBuf07));

    struct EthernetHeader ethHeader;
    ethHeader.src = newMac;
    ethHeader.dst = eth1mac;
    ethHeader.tag = htons(0x0806);

    struct ArpHeaderEthernetIPv4 arpHeader;
    arpHeader.htype = htons(1);
    arpHeader.ptype = htons(0x0800);
    arpHeader.hlen = 6;
    arpHeader.plen = 4;
    arpHeader.oper = htons(2);
    arpHeader.sender_ha = newMac; //00:99:88:77:66:55
    arpHeader.sender_pa = senderIP07; //192.168.20.20
    arpHeader.target_ha = eth1mac;
    arpHeader.target_pa = newIP07; //192.168.20.10

    memcpy(&writeBuf07, &msgHeader07, sizeof(msgHeader07));
    memcpy(&writeBuf07[sizeof(msgHeader07)], &ethHeader, sizeof(ethHeader));
    memcpy(&writeBuf07[sizeof(msgHeader07)+sizeof(ethHeader)], &arpHeader, ARP_HEADER_SIZE);
    write_all(child_stdin, writeBuf07, sizeof(writeBuf07));

    #define MAC_AS_STRING_SIZE 21
    //send 'arp' command
    char writeBuf02[GLAB_HEADER_SIZE + MAC_AS_STRING_SIZE+1];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(0);
    msgHeader.size = htons(GLAB_HEADER_SIZE + MAC_AS_STRING_SIZE+1);
    memcpy(&writeBuf02, &msgHeader, GLAB_HEADER_SIZE);
    char arp[] = "arp 192.168.1.20 eth1";
    memcpy(&writeBuf02[GLAB_HEADER_SIZE], &arp, MAC_AS_STRING_SIZE+1);
    write(child_stdin, writeBuf02, sizeof(writeBuf02));

    //read
    char readBuff_07[MAC_AS_STRING_SIZE];
    sleep(1); // wait for switch
    read(child_stdout, &readBuff_07, MAC_AS_STRING_SIZE);
    int p = GLAB_HEADER_SIZE;
     return 1;
}
*/
/**
 *  ***** TEST 08 ******
 * Test04: Load testing with 11 ARP-replies.
 * Fail! if cache for ip 192.168.1.20 on eth1 not found.
 * Expecting MAC-Address '44:55:66:77:88:14' as answer from command '$arp 192.168.1.20 eth1'
 * @param child_stdin standard input number of device
 * @param child_stdout standard output number of device
 * @return 1 on success, 0 on failure
 */
int test08(int child_stdin, int child_stdout){
    printf("TestID 08: Load testing with 11 ARP-replies\n");

    // send with ip 192.168.1.10-20
    #define IP_BEGIN 10
    #define IP_PREFIX_24 10 // length of string "192.168.1." /24 Netmask
    for (int i = 0; 10 >= i; i++) {
        struct MacAddress newMac = {0x44, 0x55, 0x66, 0x77, 0x88, 10+i};

        int length = IP_PREFIX_24 + snprintf( NULL, 0, "%d", i );
        char endIP[length]; //IP as string
        sprintf(endIP, "%s", "192.168.1.");
        sprintf(&endIP[IP_PREFIX_24], "%d", IP_BEGIN+i);
        struct in_addr newIP;
        inet_pton(AF_INET, endIP, &newIP);

        char writeBuf[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE];
        struct GLAB_MessageHeader msgHeader;
        msgHeader.type = htons(1);
        msgHeader.size = htons(sizeof(writeBuf));

        struct EthernetHeader ethHeader;
        ethHeader.src = newMac;
        ethHeader.dst = eth1mac;
        ethHeader.tag = htons(0x0806);

        struct ArpHeaderEthernetIPv4 arpHeader;
        arpHeader.htype = htons(1);
        arpHeader.ptype = htons(0x0800);
        arpHeader.hlen = 6;
        arpHeader.plen = 4;
        arpHeader.oper = htons(2);
        arpHeader.sender_ha = newMac;
        arpHeader.sender_pa = newIP;
        arpHeader.target_ha = eth1mac;
        arpHeader.target_pa = eth1ip;

        memcpy(&writeBuf, &msgHeader, sizeof(msgHeader));
        memcpy(&writeBuf[sizeof(msgHeader)], &ethHeader, sizeof(ethHeader));
        memcpy(&writeBuf[sizeof(msgHeader)+sizeof(ethHeader)], &arpHeader, ARP_HEADER_SIZE);
        write_all(child_stdin, writeBuf, sizeof(writeBuf));

    }
    #define MAC_AS_STRING_SIZE 21
    //send 'arp' command
    char writeBuf02[GLAB_HEADER_SIZE + MAC_AS_STRING_SIZE+1];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(0);
    msgHeader.size = htons(GLAB_HEADER_SIZE + MAC_AS_STRING_SIZE+1);
    memcpy(&writeBuf02, &msgHeader, GLAB_HEADER_SIZE);
    char arp[] = "arp 192.168.1.20 eth1";
    memcpy(&writeBuf02[GLAB_HEADER_SIZE], &arp, MAC_AS_STRING_SIZE+1);
/**/write(child_stdin, writeBuf02, sizeof(writeBuf02));

    //read
    char readBuff_08[MAC_AS_STRING_SIZE];
    sleep(1); // wait for switch
    read(child_stdout, &readBuff_08, MAC_AS_STRING_SIZE);
    char expectedMac[MAC_AS_STRING_SIZE] = "44:55:66:77:88:14";
    // Problem by reading on ubuntu, work only when start with index 4
    int result = memcmp(&readBuff_08[4], &expectedMac, MAC_AS_STRING_SIZE);
    char * getMac[MAC_AS_STRING_SIZE];
    if(result != 0){
        printf("TestID 08: Fail! $arp 192.168.1.20 eth1\n");
        printf("expect: %s  but got %s", expectedMac, &readBuff_08[4]);
        return 0;
    }
    printf("TestID 08: passed.\n");
    return 1;
}













