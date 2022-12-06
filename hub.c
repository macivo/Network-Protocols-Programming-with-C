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
 * @file hub.c
 * @brief Stupidly forwards network traffic between interfaces
 * @author Christian Grothoff
 */
#include "glab.h"

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


/**
 * Number of available contexts.
 */
static unsigned int num_ifc;

/**
 * All the contexts.
 */
static struct Interface *gifc;


#include "print.c"


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

  print("ForwardTo:\n");
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


static void
fwd_frame (struct Interface *src_ifc,
	   const void *frame,
	   size_t frame_size)

{
    print("ForwardFrame:\n");

    const uint8_t *cframe = frame;
    for (int i=1; i<=num_ifc; i++) {

        print("\ni: %d\n", i);
        print("dest: %s\n", gifc);
        print("src: %x\n", src_ifc[i-1]);

        const struct MacAddress *mac_src = src_ifc;
        const struct MacAddress *mac_dst = gifc;

        print("Framecontent:\n");
        for (int i = 0; i<(frame_size-1); i++) {
            print("%02x:", *(cframe + i));
        }

        if (gifc->ifc_num != src_ifc->ifc_num) {
            /*print("src IFC-no: %d\n", src_ifc->ifc_num);
            print("dst IFC-no: %d\n", gifc->ifc_num);
            print("THEY ARE NOT EQUAL!!\n");
            print("MAC-address src\n");
            print ("%02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac_src->mac[0], mac_src->mac[1],
                   mac_src->mac[2], mac_src->mac[3],
                   mac_src->mac[4], mac_src->mac[5]);
            print("MAC-address dst\n");
            print ("%02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac_dst->mac[0], mac_dst->mac[1],
                   mac_dst->mac[2], mac_dst->mac[3],
                   mac_dst->mac[4], mac_dst->mac[5]);*/
            forward_to(gifc, frame, frame_size);


        } else {
            /*print("src IFC-no: %d\n", src_ifc->ifc_num);
            print("dst IFC-no: %d\n", gifc->ifc_num);
            print("THEY ARE EQUAL!!\n");
            print("MAC-address src\n");
            print ("%02X:%02X:%02X:%02X:%02X:%02X\n",
                       mac_src->mac[0], mac_src->mac[1],
                       mac_src->mac[2], mac_src->mac[3],
                       mac_src->mac[4], mac_src->mac[5]);
            print("MAC-address dst\n");
            print ("%02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac_dst->mac[0], mac_dst->mac[1],
                   mac_dst->mac[2], mac_dst->mac[3],
                   mac_dst->mac[4], mac_dst->mac[5]);*/
        }

        *(gifc + i);
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
    print("HandleFrame:\n");
  if (interface > num_ifc)
    abort ();
  fwd_frame (&gifc[interface - 1],
	     frame,
	     frame_size);
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

    print ("%02X:%02X:%02X:%02X:%02X:%02X\n",
           mac->mac[0], mac->mac[1],
           mac->mac[2], mac->mac[3],
           mac->mac[4], mac->mac[5]);
}


#include "loop.c"

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
