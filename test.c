#include "glab.h"
#include "print.c"
#include <sys/wait.h>
#include <sys/types.h>
#define MAX_SIZE (65536 + sizeof (struct GLAB_MessageHeader))
#define MAX_IFC = 3;
struct EthernetHeader { struct MacAddress dst; struct MacAddress src; uint16_t tag;};

/////////   Mac Addresses of connected clients /////////
struct MacAddress ifc1 = {0x00,0x00,0x00,0x00,0x00,0xAA};
struct MacAddress ifc2 = {0x00,0x00,0x00,0x00,0x00,0xBB};
struct MacAddress ifc3 = {0x00,0x00,0x00,0x00,0x00,0xCC};
/////////   Mac Addresses for the test  /////////
struct MacAddress broadcast = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
struct MacAddress multicast = {0xFF,0xFF,0xFF,0xAA,0xBB,0xCC};
struct MacAddress client1 = {0x00,0x00,0x00,0x00,0x00,0x11};
struct MacAddress client2 = {0x00,0x00,0x00,0x00,0x00,0x22};
struct MacAddress client3 = {0x00,0x00,0x00,0x00,0x00,0x33};

int main (int argc, char **argv) {
    // Test starting point from Kickoff
    int cin[2], cout[2]; pipe (cin); pipe (cout);
    pid_t chld = fork ();
    if (0 == chld) {
        printf ("Starting switch in child process\n");
        close (STDIN_FILENO); close (STDOUT_FILENO);
        close (cin[1]); close (cout[0]);
        dup2 (cin[0], STDIN_FILENO);
        dup2 (cout[1], STDOUT_FILENO);
        execvp (argv[1], &argv[1]);
        printf ("Failed to run binary ‘%s’\n", argv[1]);
        exit (1);
    }
    close (cin[0]); close (cout[1]);
    int child_stdin = cin[1]; int child_stdout = cout[0];
    // End: Test starting point from Kickoff

    // init mac
    char writeBuf[sizeof(struct GLAB_MessageHeader) + MAC_ADDR_SIZE * 3];
    struct GLAB_MessageHeader header;
    header.type = htons(0);
    header.size = htons(sizeof(writeBuf));
    // ifc01, ifc02, ifc03
    memcpy(writeBuf, &header, sizeof(header));
    memcpy(&writeBuf[sizeof(header) + MAC_ADDR_SIZE * 0], &ifc1, MAC_ADDR_SIZE);
    memcpy(&writeBuf[sizeof(header) + MAC_ADDR_SIZE * 1], &ifc2, MAC_ADDR_SIZE);
    memcpy(&writeBuf[sizeof(header) + MAC_ADDR_SIZE * 2], &ifc3, MAC_ADDR_SIZE);
/***/ write_all(child_stdin, writeBuf, sizeof(writeBuf));

    // send broadcast
    printf("send:\n");

    char writeBuf2[sizeof(struct GLAB_MessageHeader) + sizeof(struct EthernetHeader)];
    struct GLAB_MessageHeader msgHeader;
    msgHeader.type = htons(1);
    msgHeader.size = htons(sizeof(writeBuf2));

    struct EthernetHeader ethHeader;
    ethHeader.src = ifc1;
    ethHeader.dst = broadcast;

    memcpy(writeBuf2, &msgHeader, sizeof(msgHeader));
    memcpy(&writeBuf2[sizeof(msgHeader)], &ethHeader, sizeof(ethHeader));

/***/ write_all(child_stdin, writeBuf2, sizeof(writeBuf2));

    // show write buffer2
    struct GLAB_MessageHeader test1;
    memcpy(&test1, &writeBuf2[0], sizeof(struct GLAB_MessageHeader));
    printf("header type: %d\n", htons(test1.type));
    struct EthernetHeader mac1;
    memcpy(&mac1, &writeBuf2[sizeof(struct GLAB_MessageHeader)], sizeof(struct EthernetHeader));
    printf("Mac Src: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac1.src.mac[0], mac1.src.mac[1], mac1.src.mac[2], mac1.src.mac[3], mac1.src.mac[4], mac1.src.mac[5]);
    printf("Mac Des: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac1.dst.mac[0], mac1.dst.mac[1], mac1.dst.mac[2], mac1.dst.mac[3], mac1.dst.mac[4], mac1.dst.mac[5]);


    // read
    printf("\nread\n");
    char readBuf[sizeof(struct GLAB_MessageHeader) + sizeof(struct EthernetHeader)];
    size_t off;
    ssize_t ret;
    off = 0;

    while(-1 != (ret = read(child_stdin, &readBuf[off], sizeof(readBuf)-off))){
        printf("start:   read   red = %d\n", (int)ret);
        if (0 >= ret) break;
        int i = 0;
        while (i < sizeof (readBuf)){
            printf("%s", &readBuf[i]);
            i++;
        }


/*        struct GLAB_MessageHeader readHeader;
        memcpy(&readHeader, readBuf, sizeof(struct GLAB_MessageHeader));
        uint16_t size = ntohs(readHeader.size);
        uint16_t type = ntohs(readHeader.type);
        printf("Got a GLAB-header size: %d  type: %d\n", size, type);

        struct EthernetHeader readEth;
        memcpy(&readEth, &readBuf[sizeof(readHeader)], size - sizeof (readHeader));
        printf("Mac : %02X:%02X:%02X:%02X:%02X:%02X\n",
        readEth.dst.mac[0], readEth.dst.mac[1], readEth.dst.mac[2], readEth.dst.mac[3], readEth.dst.mac[4], readEth.dst.mac[5]);*/


//        off += ret;

//
//

/*        while(off > sizeof(struct GLAB_MessageHeader)) {

            size = ntohs(readHeader.size);

            if (off < size) break;
            if (size < sizeof(struct GLAB_MessageHeader)) abort();

            if (0 != ntohs(readHeader.type)){
                printf("get a message msg with eth-frame\n");

                printf("Mac DES: %02X:%02X:%02X:%02X:%02X:%02X\n",
                       readEth.src.mac[0], readEth.src.mac[1], readEth.src.mac[2], readEth.src.mac[3], readEth.src.mac[4], readEth.src.mac[5]);
            }
            memmove(readBuf, &readBuf[size], off - size);
            off -= size;
        }*/
    }

    sleep(1);
    // stop child process
    kill (chld, SIGKILL);
    print ("Test endet!\n");
    return 0;
}