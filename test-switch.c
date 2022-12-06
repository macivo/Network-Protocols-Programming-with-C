#include "glab.h"
#include "print.c"
//for using pid_t:
#include <sys/types.h>

/**
 * Maximum size of a message.
 */
#define MAX_SIZE (65536 + sizeof (struct GLAB_MessageHeader))
#define MAX_IFC 3
#define GLAB_HEADER_SIZE sizeof(struct GLAB_MessageHeader)
#define ETHERNET_HEADER_SIZE sizeof(struct EthernetHeader)

struct EthernetHeader {
    struct MacAddress dst;
    struct MacAddress src;
    uint16_t tag;
};

size_t FRAME_SIZE = sizeof(struct GLAB_MessageHeader)+sizeof(struct EthernetHeader);
size_t FRAME_0_SIZE = sizeof(struct GLAB_MessageHeader)+3*MAC_ADDR_SIZE;

/////////   Mac Addresses of connected clients /////////
struct MacAddress eth1 = {0x00, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
struct MacAddress eth2 = {0x00, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
struct MacAddress eth3 = {0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
/////////   Mac Addresses for the test  /////////
struct MacAddress broadcast = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
struct MacAddress multicast = {0xFF, 0xFF, 0xFF, 0xAA, 0xBB, 0xCC};
struct MacAddress client1 = {0x00, 0x11, 0x11, 0x11, 0x11, 0x11};
struct MacAddress client2 = {0x00, 0x22, 0x22, 0x22, 0x22, 0x22};
struct MacAddress client3 = {0x00, 0x33, 0x33, 0x33, 0x33, 0x33};


int main(int argc, char **argv) {
    // Test starting point from Kickoff
    int cin[2], cout[2];
    pipe(cin);
    pipe(cout);
    int chld = fork();
    char *start_arr[5];
    start_arr[0] = argv[1];
    start_arr[1] = " 1";
    start_arr[2] = " 2";
    start_arr[3] = " 3";
    start_arr[4] = NULL;

    if (0 == chld) {
        printf("Starting switch in child process\n");
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
    sleep(1);

    /////////   init    /////////
    uint8_t writeBuf[GLAB_HEADER_SIZE + MAC_ADDR_SIZE * 3];
    struct GLAB_MessageHeader header;
    header.type = htons(0);
    header.size = htons(sizeof(writeBuf));

    // ifc01, ifc02, ifc03
    memcpy(writeBuf, &header, sizeof(header));
    memcpy(&writeBuf[GLAB_HEADER_SIZE + MAC_ADDR_SIZE * 0], &eth1, MAC_ADDR_SIZE);
    memcpy(&writeBuf[GLAB_HEADER_SIZE + MAC_ADDR_SIZE * 1], &eth2, MAC_ADDR_SIZE);
    memcpy(&writeBuf[GLAB_HEADER_SIZE + MAC_ADDR_SIZE * 2], &eth3, MAC_ADDR_SIZE);
/**/write_all(child_stdin, writeBuf, sizeof(writeBuf));


    ///////// test01: Broadcast /////////
    int result01 = test01(child_stdin,child_stdout);

    ///////// test02: Sending a frame with a dst-address which is in switching table per unicast /////////
    int result02 = test02(child_stdin,child_stdout);

    /////////   Test03: Sending a frame with a dst-address which is not in switching table. /////////
    int result03 = test03(child_stdin,child_stdout);

    /////////   Test04: Load testing with 100 MAC-addresses. /////////
    int result04 = test04(child_stdin,child_stdout);

    /////////   Test05: Sending incomplete Frame (frame length shorter than header) /////////
    int result05 = test05(child_stdin,child_stdout);

    /////////   Test06: Device that is already known is plugged into another port (broadcast and sending another frame)  /////////
    int result06 = test06(child_stdin,child_stdout);

    ///////// test results ////////////
    int result = result01+result02+result03+result04+result05+result06;
    printf("\nResult: %d/6 passed\n", result);


    // stop child process
    kill(chld, SIGKILL);
    printf("Test procedure complete!\n");

    if (6 == result) {
        return 0;
    }else {
        return -1;
    }
}

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
 * Print MAC-address
 * @param mac MAC-address to be printed
 */
void print_mac (struct MacAddress mac){
    printf("Mac: %02X:%02X:%02X:%02X:%02X:%02X",
           mac.mac[0], mac.mac[1], mac.mac[2], mac.mac[3], mac.mac[4], mac.mac[5]);
};


/**
 ***** TEST 01 ******
 * A newly connected device sends a broadcast.
 * Failed if the message is not send to all participants except the one who sends the broadcast message.
 * @param child_stdin - standard input number of switch
 * @param child_stdout - standard output number of switch
 * @return 1 on success, 0 on fail
 */

int test01(int child_stdin, int child_stdout){

    // send broadcast
    printf("Test01: Newly connected device sends broadcast\n");
    int passed = 1;

    char writeBuf1[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(1);
    msgHeader.size = htons(sizeof(writeBuf1));

    struct EthernetHeader ethHeader;
    ethHeader.src = client1;
    ethHeader.dst = broadcast;

    memcpy(writeBuf1, &msgHeader, sizeof(msgHeader));
    memcpy(&writeBuf1[sizeof(msgHeader)], &ethHeader, sizeof(ethHeader));

    write_all(child_stdin, writeBuf1, sizeof(writeBuf1));

    // show write buffer1
    struct GLAB_MessageHeader test1;
    memcpy(&test1, &writeBuf1[0], sizeof(struct GLAB_MessageHeader));
    struct EthernetHeader mac1;
    memcpy(&mac1, &writeBuf1[sizeof(struct GLAB_MessageHeader)], sizeof(struct EthernetHeader));

    // read
    char readBuf1[MAX_SIZE];
    size_t off;
    ssize_t ret;
    off = 0;
    sleep(1);
    ret = read(child_stdout, &readBuf1[off], sizeof(readBuf1) - off);
    struct GLAB_MessageHeader readGHeader;
    uint16_t size;
    off += ret;

    memcpy(&readGHeader, readBuf1, sizeof(readGHeader));

    while (off > GLAB_HEADER_SIZE) {
        memcpy(&readGHeader, readBuf1, GLAB_HEADER_SIZE);
        size = ntohs(readGHeader.size);

        if (off < size) break;
        if (size < GLAB_HEADER_SIZE) abort();

        struct EthernetHeader readEHeader;
        memcpy(&readEHeader, &readBuf1[GLAB_HEADER_SIZE], ETHERNET_HEADER_SIZE);

        //check if
        struct MacAddress macSrc = readEHeader.src;
        struct MacAddress macDst = readEHeader.dst;

        //should not sending the frame to itself
        if(1 == ntohs(readGHeader.type)) {
            passed = -1;
        }

        //should send the frame to device 2 and 3
        if(2 == ntohs(readGHeader.type)) {
            int checkSrc = maccomp(&macSrc, &client1);
            if (0 != checkSrc) {
                passed = 0;
            }
            int checkDst = maccomp(&macDst, &broadcast);
            if (0 != checkDst) {
                passed = 0;
            }
        }

        if(3 == ntohs(readGHeader.type)) {
            int checkSrc = maccomp(&macSrc, &client1);
            if (0 != checkSrc) {
                passed = 0;
            }
            int checkDst = maccomp(&macDst, &broadcast);
            if (0 != checkDst) {
                passed = 0;
            }
        }

        memmove(readBuf1, &readBuf1[size], off - size);
        off -= size;
    }

    if(1 == passed ) {
        printf("Test01: succeeded\n");
    } else {printf("Test01: failed\n");
    }
    return passed;
}


/**
 *  ***** TEST 02 ******
 * Sending a frame with a dst-address which is in switching table per unicast
 * Fails, if the message is not send via unicast. Fails, if the output of MAC-address or interface number does not match
 * the input
  *@param child_stdin standard input number of switch
 * @param child_stdout standard output number of switch
 * @return 1 on success, 0 on fail
 */
int test02(int child_stdin, int child_stdout){
    printf("Test02: sending frame via unicast\n");
    char writeBuf3[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(2);
    msgHeader.size = htons(sizeof(writeBuf3));

    struct EthernetHeader ethHeader;
    ethHeader.src = client2;
    ethHeader.dst = client1;

    memcpy(writeBuf3, &msgHeader, sizeof(msgHeader));
    memcpy(&writeBuf3[sizeof(msgHeader)], &ethHeader, sizeof(ethHeader));

    write_all(child_stdin, writeBuf3, sizeof(writeBuf3));

    // show write buffer2
    struct GLAB_MessageHeader test1;
    memcpy(&test1, &writeBuf3[0], sizeof(struct GLAB_MessageHeader));

    struct EthernetHeader mac1;
    memcpy(&mac1, &writeBuf3[sizeof(struct GLAB_MessageHeader)], sizeof(struct EthernetHeader));

    // read

    char readBuf[MAX_SIZE];
    ssize_t return_val;
    sleep(1);
    return_val = read(child_stdout, &readBuf, sizeof(readBuf));

    struct GLAB_MessageHeader readGHeader;
    if (FRAME_SIZE != return_val) {
        printf("Test02: failed\n");
        return 0;
    }
    memcpy(&readGHeader, readBuf, sizeof(readGHeader));
    memcpy(&readGHeader, readBuf, GLAB_HEADER_SIZE);
    struct EthernetHeader readEHeader;
    memcpy(&readEHeader, &readBuf[GLAB_HEADER_SIZE], ETHERNET_HEADER_SIZE);
    int ifc = ntohs(readGHeader.type);
    int mac_same = maccomp(&readEHeader.dst, &client1);
    if (1 == ifc && 0 == mac_same) {
        printf("Test02: succeeded\n");
        return 1;
    } else {
        printf("Test02: failed\n");
        return 1;
    }

}

/**
 *  ***** TEST 03 ******
 * Test03: Sending a frame with a destination MAC-address which is not in switching table.
 * Fails, if switch do not make a broadcast. Expecting 2 frames to be received.
 * @param child_stdin standard input number of switch
 * @param child_stdout standard output number of switch
 * @return 1 on success, 0 on fail
 */
int test03(int child_stdin, int child_stdout){
    printf("Test03: Checking with unknown destination MAC-address.\n");
    // A MAC Adresse is not contained in switching table
    struct MacAddress newMac = {0x00, 0x55, 0x66, 0x77, 0x88, 0x99};

    // send
    char writeBuf2[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(1);
    msgHeader.size = htons(sizeof(writeBuf2));

    struct EthernetHeader ethHeader;
    ethHeader.src = client1;
    ethHeader.dst = newMac;

    memcpy(writeBuf2, &msgHeader, sizeof(msgHeader));
    memcpy(&writeBuf2[sizeof(msgHeader)], &ethHeader, sizeof(ethHeader));
    write_all(child_stdin, writeBuf2, sizeof(writeBuf2));

// read
    char readBuf[MAX_SIZE];
    struct GLAB_MessageHeader readGHeader;
    size_t off = 0;    ssize_t ret;     uint16_t size;
    sleep(1); // wait for switch
    ret = read(child_stdout, &readBuf[off], sizeof(readBuf) - off);

    if(0 == ret) {
        printf("Test03: failed, null frame received\n");
        return 0;
    }
    if(0 > ret) {
        printf("Test03: failed: Error getting frames\n");
        return 0;
    }

    int twoFrames = 0; //expect 2 frames received
    off += ret;
    while (off > GLAB_HEADER_SIZE) {
        memcpy(&readGHeader, readBuf, GLAB_HEADER_SIZE);
        size = ntohs(readGHeader.size);
        int type;
        if (off < size) break;
        if (size < GLAB_HEADER_SIZE) abort();

        struct EthernetHeader readEHeader;
        memcpy(&readEHeader, &readBuf[GLAB_HEADER_SIZE], ETHERNET_HEADER_SIZE);

        if (2 == ntohs(readGHeader.type) || 3 == ntohs(readGHeader.type)){
            if(maccomp(&client1, &readEHeader.src) != maccomp(&newMac, &readEHeader.dst)) {
                printf("Test03: failed: Received an unexpected frame by interface nr. %d.\n", ntohs(readGHeader.type));
                print_mac(client1);             printf("<< Expect SCR\n");
                print_mac(readEHeader.src);     printf("<< Received SCR\n");
                print_mac(newMac);              printf("<< Expect DST\n");
                print_mac(readEHeader.dst);     printf("<< Received DST\n");
                return 0;
            }
            twoFrames++;
        } else {
            printf("Test03: failed: Received an unexpected frame by interface nr. %d.\n", ntohs(readGHeader.type));
            return 0;
        }
        memmove(readBuf, &readBuf[size], off - size);
        off -= size;
    }

    if (2 != twoFrames) {
        printf("Test03 fail!!!: Expect 2 frames, but %d frame(s) received.\n", twoFrames);
        return 0;
    }
    printf("Test03: succeeded\n");
    return 1;
}

/**
 *  ***** TEST 04 ******
 * Test04: Load testing with 100 MAC-addresses.
 * Fails, if switch do not make a broadcast.
 * Expecting 200 frames to be received. Last frame must contain the last MAC-address
 * @param child_stdin standard input number of switch
 * @param child_stdout standard output number of switch
 * @return 1 on success, 0 on fail
 */
int test04(int child_stdin, int child_stdout){
    printf("Test04: Load testing with 100 MAC-addresses\n");
    // A MAC Adresse is not contained in switching table


    // send
    for (int i = 1; 100 >= i; i++){
        struct MacAddress newMac = {0x00, 0x55, 0x66, 0x77, 0x88, i};
        char writeBuf2[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE];
        struct GLAB_MessageHeader msgHeader;
        msgHeader.type = htons(1);
        msgHeader.size = htons(sizeof(writeBuf2));

        struct EthernetHeader ethHeader;
        ethHeader.src = client1;
        ethHeader.dst = newMac;

        memcpy(writeBuf2, &msgHeader, sizeof(msgHeader));
        memcpy(&writeBuf2[sizeof(msgHeader)], &ethHeader, sizeof(ethHeader));
        write_all(child_stdin, writeBuf2, sizeof(writeBuf2));
    }


    // read
    char readBuf[MAX_SIZE];
    struct GLAB_MessageHeader readGHeader;
    size_t off = 0;    ssize_t ret;     uint16_t size;
    sleep(1); // wait for switch
    ret = read(child_stdout, &readBuf[off], sizeof(readBuf) - off);

    if(0 == ret) {
        printf("Test04: failed: null frame received\n");
        return 0;
    }
    if(0 > ret) {
        printf("Test04: failed: Error by getting frames\n");
        return 0;
    }

    int twoHundredFrames = 0;
    off += ret;
    struct EthernetHeader readEHeader;
    while (off > GLAB_HEADER_SIZE) {
        memcpy(&readGHeader, readBuf, GLAB_HEADER_SIZE);
        size = ntohs(readGHeader.size);
        int type;
        if (off < size) break;
        if (size < GLAB_HEADER_SIZE) abort();
        memcpy(&readEHeader, &readBuf[GLAB_HEADER_SIZE], ETHERNET_HEADER_SIZE);
        twoHundredFrames++;
        memmove(readBuf, &readBuf[size], off - size);
        off -= size;
    }
    // check the last frame
    struct MacAddress expectMac = {0x00, 0x55, 0x66, 0x77, 0x88, 100};
    if(maccomp(&client1, &readEHeader.src) != maccomp(&expectMac, &readEHeader.dst)) {
        printf("Test04: failed: Received an unexpected last frame\n");
        print_mac(client1);             printf("<< Expect SCR\n");
        print_mac(readEHeader.src);     printf("<< Received SCR\n");
        print_mac(expectMac);              printf("<< Expect DST\n");
        print_mac(readEHeader.dst);     printf("<< Received DST\n");
        return 0;
    }
    if (200 != twoHundredFrames) {
        printf("Test04: failed: Expect 200 frames, but %d frame(s) received\n", twoHundredFrames);
        return 0;
    }
    printf("Test04: succeeded\n");
    return 1;
}

/**
 *  ***** TEST 05 ******
 * Sending incomplete Frame (frame length shorter than header)
  *@param child_stdin standard input number of switch
 * @param child_stdout standard output number of switch
 * @return 1 on success, 0 on fail
 */
 
int test05(int child_stdin, int child_stdout){
    printf("Test05: sending malformed frame\n");
    char writeBuf5[GLAB_HEADER_SIZE];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(2);
    msgHeader.size = htons(FRAME_SIZE);

    memcpy(writeBuf5, &msgHeader, sizeof(msgHeader));

    write(child_stdin, writeBuf5, FRAME_SIZE);

    // read
    char readBuf5[MAX_SIZE];
    ssize_t return_val = read(child_stdout, &readBuf5, sizeof(readBuf5));
    sleep(1);
    if (FRAME_SIZE != return_val) {
        printf("Test05: succeeded\n");
        return 1;
    } else {
        printf("Test05: failed\n");
        return 0;
    }
}

/**
 *  ***** TEST 06  ******
 *  A device that is already known is plugged into another port
 *@param child_stdin standard input number of switch
 * @param child_stdout standard output number of switch
 * @return 1 on success, 0 on fail
 */
int test06(int child_stdin, int child_stdout){

    // device client1 that was on interface 1 is now on interface 3
    printf("Test06: Device that is already known is plugged into another port\n");
    int passed = 1;

    //client 1 on interface 3 is sending a message to client 2
    char writeBufA[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE];
    struct GLAB_MessageHeader msgHeaderA;
    msgHeaderA.type = htons(3);
    msgHeaderA.size = htons(sizeof(writeBufA));

    struct EthernetHeader ethHeaderA;
    ethHeaderA.src = client1;
    ethHeaderA.dst = client2;

    memcpy(writeBufA, &msgHeaderA, sizeof(msgHeaderA));
    memcpy(&writeBufA[sizeof(msgHeaderA)], &ethHeaderA, sizeof(ethHeaderA));

    write_all(child_stdin, writeBufA, sizeof(writeBufA));

    //next frame sending:
    //client 2 (interface 2) is sending a message to client1 (interface 3)
    char writeBuf1[GLAB_HEADER_SIZE + ETHERNET_HEADER_SIZE];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(2);
    msgHeader.size = htons(sizeof(writeBuf1));

    struct EthernetHeader ethHeader;
    ethHeader.src = client2;
    ethHeader.dst = client1;

    memcpy(writeBuf1, &msgHeader, sizeof(msgHeader));
    memcpy(&writeBuf1[sizeof(msgHeader)], &ethHeader, sizeof(ethHeader));

    write_all(child_stdin, writeBuf1, sizeof(writeBuf1));

    // read if frame from client2 (interface 2) arrives at client1 (interface 3)
    char readBuf1[MAX_SIZE];
    size_t off;
    ssize_t ret;
    off = 0;
    sleep(1);
    ret = read(child_stdout, &readBuf1[off], sizeof(readBuf1) - off);
    struct GLAB_MessageHeader readGHeader;
    uint16_t size;
    off += ret;

    memcpy(&readGHeader, readBuf1, sizeof(readGHeader));

    while (off > GLAB_HEADER_SIZE) {
        memcpy(&readGHeader, readBuf1, GLAB_HEADER_SIZE);
        size = ntohs(readGHeader.size);

        if (off < size) break;
        if (size < GLAB_HEADER_SIZE) abort();

        struct EthernetHeader readEHeader;
        memcpy(&readEHeader, &readBuf1[GLAB_HEADER_SIZE], ETHERNET_HEADER_SIZE);

        //check
        struct MacAddress macSrc = readEHeader.src;
        struct MacAddress macDst = readEHeader.dst;
        int ifc = ntohs(readGHeader.type);

        // check if src is client2 and dst is client 1 and interface is only 3
        if (0 == (maccomp(&macSrc, &client2) && maccomp(&macDst, &client1)) &&  (3 != ifc)) {

            passed = 0;
        }
        memmove(readBuf1, &readBuf1[size], off - size);
        off -= size;
    }

    if(1 == passed ) {
        printf("Test06: succeeded\n");
    } else {printf("Test06: failed\n");
    }
    return passed;
}