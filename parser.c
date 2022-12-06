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
 * @file sample_parser.c
 * @brief Parses output of network-driver
 * @author Christian Grothoff
 */
#include "glab.h"
#include "print.c"

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
    const int FRAME_READER_SIZE = 16;
    const uint8_t *cframe = frame;
    print ("\nInterface: %d\n", interface);
    for (int i = 0; i<(FRAME_READER_SIZE-1); i++) {
    //for (int i = 0; i<(frame_size-1); i++) {
      //  print("%02x", *(cframe + i));
        if (i==0) {
            print("\nDestination MAC: \n");
            print("%02x:", *(cframe + i));
        }
        else if (i==5) {
            print("%02x\n", *(cframe + i));
        }
        else if (i==6) {
            print("Source MAC: \n");
            print("%02x:", *(cframe + i));
        }
        else if (i==11) {
            print("%02x\n", *(cframe + i));
        }
        else if (i==12) {
            print("Type: 0x%02x", *(cframe + i));
        }
        else if (i==13) {
            print("%02x\n", *(cframe + i));
        }
        else if (i==14) {
            print("Header length: %02x\n", *(cframe + i));
        }else {
            print("%02x:", *(cframe + i));
        }
    }
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
  print ("Received command `%s'\n",
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
  loop ();
  return 0;
}
