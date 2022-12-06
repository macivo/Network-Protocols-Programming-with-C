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
 * @file switch.c
 * @brief Ethernet switch
 * @author Christian Grothoff
 */
#include "glab.h"
#include "print.c"


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
  uint16_t tag;
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
    * Number of this interface.
    */
    uint16_t ifc_num;

};

#include <time.h>

/**
 * A switching connection containing an interface of the switch that connects the MAC-address of a switch-port to the MAC-address
 * of a connected device. It also contains a timestamp, when this switching connection was last used.
 */
struct Switching_connection {
    struct Interface switch_ifc;
    struct MacAddress device_mac;
    time_t timestamp;
};

#define SWITCHING_TABLE_SIZE 16 //can vary, we set the table size to 16
#define MAC_ADDR_SIZE 6

/**
 * All the switching connections in an array
 */
static struct Switching_connection switching_table[SWITCHING_TABLE_SIZE];

/**
 * Number of table entries
 * Gets incremented with each new entry. Max = SWITCHING_TABLE_SIZE
 */

static unsigned int num_table_entries = 0;

/**
 * Number of available contexts/interfaces
 */
static unsigned int num_ifc;

/**
 * All the contexts/interfaces
 */
static struct Interface *gifc;


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
 * compare method for 2 mac-addresses, taken from faq-sheet Prof. Grothoff & Prof. Wenger
 * @param mac1 first mac-address
 * @param mac2 second mac-address
 * @return 0 if both are equal. any other value: they are not equal
 */
const int
maccomp (const struct MacAddress *mac1,
         const struct MacAddress *mac2) {
    return memcmp (mac1, mac2, sizeof(struct MacAddress));
};

/**
 * Checks, if the source mac address matches the mac address of an interface. If so, the frame has to be dismissed
 * @param mac_addr_search the mac-address we want to look up
 * @return 1 if the src-mac-address is actually a mac-address of an interface = dismiss frame. 0 if the src-mac-address does not match an interface-mac
 */
static int
src_is_switch(struct MacAddress *mac_addr_search) {
  for(int i = 0; i < num_ifc; i++) {
      struct Interface *ifc = gifc+i;
      if (0==maccomp(&ifc->mac, mac_addr_search)) {
          return 1;
      }
  }
  return 0;
};

/**
 * checks, if a mac-address is already included in the switching table. If it is already included,
 * update timestamp and interface, in case the interface has changed.
 * @param mac_addr1 the address to be checked
 * @return 1 if it is found, 0 if it is not found or table is empty, -1 if the src address is a switch address (in that case, dismiss frame)
 */
static int
lookup (struct MacAddress *mac_addr_search, struct Interface *ifc) {
    if (1 == src_is_switch(mac_addr_search)) {
        return -1;
    } else if (0 == num_table_entries) {
        return 0;
    } else {
        for (int i=0; i<num_table_entries; i++) {
            struct MacAddress *mac_addr_table = &switching_table[i].device_mac;
            if (0==maccomp(mac_addr_search, mac_addr_table)) {
                switching_table[i].timestamp = time(NULL);
                switching_table[i].switch_ifc = *ifc;
                const int time = switching_table[i].timestamp;
                return 1;
            }
        }
        return 0;
    }
};

/**
 * In the switching table search for the entry with the oldest timestamp (= the least recently used entry)
 * @return the index of the entry to be overwritten
 */

static int
search_oldest_entry() {
    time_t timestamp_persisted = switching_table[0].timestamp; //persist timestamp of first table entry
    int override_index = 0;
    for (int i=1; i<num_table_entries; i++) {
        if (timestamp_persisted > switching_table[i].timestamp) { //compare this entrys timestamp with the one persisted. if it is smaller, persist it.
            override_index = i;
        }
    }
    return override_index;
};

/**
 * create a new entry for switching table and add it in case the table is not full yet
 * @param ifc pointer to the interface for new entry
 * @param src_address mac adress of device for new entry
 */

static void
add_table_entry(struct Interface *ifc, struct MacAddress *src_address) {
    struct Switching_connection sc; //create a new switching connection for table
    sc.switch_ifc = *ifc;
    sc.device_mac = *src_address;
    sc.timestamp = time(NULL);
    memcpy(&switching_table[num_table_entries],
           &sc,
           sizeof(sc));
    num_table_entries += 1;
};

/**
 * In case the table is full (more than 16 entries): Override the entry with the least recently used timestamp
 * @param ifc the new interface for the entry to be overwritten
 * @param src_address the new device mac-address for the entry to be overwritten
 */
static void
override_table_entry(struct Interface *ifc, struct MacAddress *src_address) {
    int override_index = search_oldest_entry(); //search for oldest timestamp
    struct Switching_connection sc; //create a new entry for switching table
    sc.switch_ifc = *ifc;
    sc.device_mac = *src_address;
    sc.timestamp = time(NULL);
    //copy the new entry into the switching table
    memcpy(&switching_table[override_index],
           &sc,
           sizeof(sc));

};

/**
 * Check, if the frame has to be send via broadcast/multicast.
 * Done by checking the IG Bit of the first Byte (first Bit) is set by taking mod2 of the first Byte.
 * If the result is 1, the IG Bit is set; the MAC-address is therefore a broadcast or multicast address
 * If the result is 0, the MAC-Address is a unicast address
 *
 * @param mac_addr_dst the mac-address to check whether it is multicast/broadcast or unicast
 * @return an integer value 1 for broadcast/multicast, 0 for unicast
 */
static int
check_broadcast(struct MacAddress *mac_addr_dst) {
    const uint8_t c_dest_address = mac_addr_dst->mac[0];
    int return_value = c_dest_address%2;
    return return_value;
};

/**
 * Get the interface of destination by looking it up in the switching table (which interface belongs to the dst-mac)
 * @param mac_addr_search the dst-mac-address to be looked up in the switching table
 * @return the dst-interface or 0 if there is none found
 */
static struct Interface *
get_dst_ifc(struct MacAddress *mac_addr_search) {

        for (int i=0; i<num_table_entries; i++) {
            struct Interface *lookedup_interface = &switching_table[i].switch_ifc;
            struct MacAddress *mac_addr_table = &switching_table[i].device_mac;

            if (0==maccomp(mac_addr_search, mac_addr_table)) {
                return lookedup_interface;
            }
        }
        return 0;
};

/**
 * Send a frame to all available interfaces by referencing *gifc, a pointer to all detected interfaces
 * @param src_interface the interface where the frame came from
 * @param frame the frame to be passed
 * @param frame_size the size of the frame to be passed
 */
static void
send_broadcast(struct Interface *src_interface, const void *frame, size_t frame_size) {
    for (int i=0; i<num_ifc; i++) {
        if (src_interface!=&gifc[i]) {
            forward_to(&gifc[i], frame, frame_size);
        }
    }
};

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
	     size_t frame_size) {
    struct EthernetHeader eh;

    if (frame_size < sizeof(eh)) {
        fprintf(stderr,
                "Malformed frame\n");
        return;
    }
    memcpy(&eh,
           frame,
           sizeof(eh));

    /**
     * Check, if src is already known in the switching table. Add or update an entry if needed
     */
    struct MacAddress *dest_address = &eh.dst;
    struct MacAddress *src_address = &eh.src;

    int lookup_res = lookup(src_address, ifc);
    if (1 == lookup_res || -1 == lookup_res) { //1 = a table entry for this mac-address already exists, -1 = this src-address belongs to switch
        // do nothing
    } else if (num_table_entries >= SWITCHING_TABLE_SIZE){
        override_table_entry(ifc, src_address);
    }else { //add table entry
       add_table_entry(ifc, src_address);
    }

    /**
     * Check here, if the destination is multicast/broadcast (for this task the same) or unicast
     */

    if (1==check_broadcast(dest_address)) {
        send_broadcast(ifc, frame, frame_size);
    } else {
        struct Interface *dest_ifc = get_dst_ifc(dest_address);
        if (dest_ifc != 0) { //Check, if the destination mac-address can be found in the switching table
            forward_to(dest_ifc, frame, frame_size);
        } else { //if the destination is not in the switching table, send this frame to broadcast!
            send_broadcast(ifc, frame, frame_size);
        }
    }

};


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
};


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
  cmd[cmd_len - 1] = '\0';
  print ("Received command `%s' (ignored)\n",
	 cmd);
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
 * Launches the switch.
 *
 * @param argc number of arguments in @a argv
 * @param argv binary name, followed by list of interfaces to switch between
 * @return not really
 */
int
main (int argc,
      char **argv)
{
  struct Interface ifc[argc - 1];

  memset (ifc,
	  0,
	  sizeof (ifc));
  num_ifc = argc - 1;
  gifc = ifc;
  for (unsigned int i=1;i<argc;i++)
    ifc[i-1].ifc_num = i;

  loop ();
  return 0;
}
