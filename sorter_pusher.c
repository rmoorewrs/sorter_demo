/*
* 01/21/16-initial code written, Rich Moore, richard.moore@windriver.com,
* link with libmodbus v3.0.x
*
* This program controls a virtual conveyor belt sorter implemented in
* Factory IO. This is one of the default pre-built scenarios in Factory IO
* Communication is via modbus tcp, but the communication layer has been
* abstracted out by the plc library. The plc library implements a modbus
* client(master) that sends requests to the Factory IO simulation which must
* be configured as a Modbus TCP Server. The program must be linked with
* libmodbus 3.0.x.
*
* The plc_t structure contains a state variable table which reflects the
* state of the remote device IO as of last update via plc_state_update().
*
* No ladder logic function is available so we have to set up output states
* and define under which input conditions to apply the right output states.
*
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <netinet/in.h>
#include <linux/tipc.h>
#include <modbus.h>
#include <vplc.h>
#include <sorter_pusher.h>

#define IP_PORT           02
#define	MB_SLAVE_ID       1
#define LOOP_PERIOD_USEC  40000	/* 20Hz is a period of 50ms or 50000 usec */

/*
 * This is all specific to the sorter pre-built demo in values for
 * conveyor belt control.
 */
#define DEMO_DIG_INPUTS    DI_EOL
#define DEMO_DIG_OUTPUTS   DO_EOL

void sorter_reset (plc_t *plc, plc_state_t *svt);

int main (int argc, char *argv[])
{
    plc_t          *plc;
    plc_state_t    *svt;
    unsigned long  loop_count = 0;
    int            box_type = BOX_TYPE_UNKNOWN;
    int            this_box = 0, box_code;
    char           ip_address[80];
    int            reset_command = FALSE;
    int            instance;

    /* read command line args */
    if (argc <= 2) {
	fprintf (stderr,
                 "Usage:\n%s <modbus_slave_ip_address> <instance (1 or 2) "
                 "or 'r' for reset> \n", argv[0]);
        exit (-1);
    }

    if (argv[1]) {
        strncpy (ip_address, argv[1], 17);
    } else {
        fprintf (stderr,
                 "%s: you must specify ip address for modbus or modbus reset\n",
                 argv[0]);
        exit (-1);
    }
			
    if (*argv[2] == 'r') {
        reset_command = TRUE;
    } else if (*argv[2] == '1') {
        instance = 1;
    } else if (*argv[2] == '2') {
        instance = 2;
    } else {
	fprintf (stderr,
                 "Usage:\n%s <modbus_slave_ip_address> <instance (1 or 2) "
                 "or 'r' for reset> \n", argv[0]);
        exit (-1);
    }

    /* allocate a state variable table */
    svt = malloc (sizeof (plc_state_t));
    if (!svt) {
        fprintf (stderr,
                 "Unable to allocate memory for state variable table\n");
        exit (-1);
    }

    /* initialize the virtual plc */
    plc = plc_init_modbus (ip_address, instance, IP_PORT, MB_SLAVE_ID,
                           DEMO_DIG_INPUTS, DEMO_DIG_OUTPUTS, FALSE);
    if (!plc)
        exit (-1);

    if (reset_command == TRUE) {
        sorter_reset (plc, svt);
        plc_shutdown (plc);
        exit (0);
    }

    /* Initialize the heartbeat */
    plc_init_heartbeat (plc);
	
    /* skip the reset if not active */
    if (plc->mode == MODE_ACTIVE)
        svt->current_state = STATE_NOT_READY;
    else
        svt->current_state = STATE_READY;

    /* 
     * This is the main loop, which runs forever at a fixed period (20ms) 
     */
    printf ("Entering main loop\n");
    while (1) {

    /* read the latest state variable table, copy into svt */
    plc_state_read (plc, svt);

    switch (svt->current_state) {

    case STATE_NOT_READY:
        /* reset the sorter into a ready configuration */
        sorter_reset (plc, svt);
        // now we should be ready */
        svt->current_state = STATE_READY;
        break;

    case STATE_READY:
        printf("%s - %s\n",
               state_strings[svt->current_state],
               (plc->mode == MODE_ACTIVE ? "ACTIVE":"STANDBY"));

        /* we're ready, so turn on box emitter and go to next state */
        plc_state_read (plc, svt);
        svt->DO[DO_EMITTER_01] = TRUE;
        plc_state_write (plc, svt); /* update will be done at end of loop */

        /* now wait for box to arrive at first sensor outside of emitter */
        svt->current_state=STATE_WAITING_FOR_BOX_AT_GATE_01;
        break;


    case STATE_WAITING_FOR_BOX_AT_GATE_01:
        /* detecting box at gate */
        if (svt->DI[DI_DIFFUSE_03]) {
            printf ("Box Detected at Gate 01\n");
            svt->current_state = STATE_BOX_AT_GATE_01;
            /* turn off box emitter */
            svt->DO[DO_EMITTER_01] = FALSE;
        }
        break;

    case STATE_BOX_AT_GATE_01:
        /* we have to identify the box type */
        box_code = 0;
        printf ("Detecting box type at gate...");

        /* make a code byte from 3 sensor values */
        this_box =
            (svt->DI[DI_RETROREF_NOT_01] == TRUE ? 0x00 : 0x01) +
             (svt->DI[DI_DIFFUSE_02] == TRUE ? 0x10 : 0x00) +
             (svt->DI[DI_DIFFUSE_03] == TRUE ? 0x100 : 0x00);

        switch (this_box) {
        case 0x00:
            box_type = BOX_TYPE_UNKNOWN;
            this_box = 0;
            break;
        case 0x0111:
            box_type = BOX_TYPE_01;
            this_box = 1;
            break;
        case 0x110:
            box_type = BOX_TYPE_02;
            this_box = 2;
            break;
        case 0x100:
            box_type = BOX_TYPE_03;
            this_box = 3;
            break;
        default:
            break;
        }

        if (this_box != 0) {
            svt->current_state=STATE_BOX_DETECTED;
            printf ("Detected box type %d\n", this_box);
        } else {
            printf ("Error detecting box type\n");
            svt->current_state = STATE_ERROR;
        }
        break;

    case STATE_BOX_DETECTED:
        /* lower first gate, make sure 2nd is up */
        printf ("STATE_BOX_DETECTED: lowering gate 01\n");
        svt->DO[DO_STOP_BLADE_01] = FALSE;
        svt->DO[DO_STOP_BLADE_02] = TRUE;
        svt->current_state = STATE_WAITING_FOR_BOX_AT_GATE_02;
        break;

    case STATE_WAITING_FOR_BOX_AT_GATE_02:	
        if (svt->DI[DI_DIFFUSE_04] == TRUE) {
            svt->current_state = STATE_BOX_WEIGHING;

            // svt->DO[DO_STOP_BLADE_01]=TRUE; // raise the first gate again
        }
        break;

    case STATE_BOX_WEIGHING:
        /* this is a placeholder for when analog variables are added */
        printf ("Weighing Box type %d\n", this_box);
        svt->current_state = STATE_BOX_WEIGHED;
        break;

    case STATE_BOX_WEIGHED:
        svt->DO[DO_STOP_BLADE_02] = FALSE; /* lower the second gate */
        svt->current_state = STATE_BOX_PRE_PUSH;
        printf ("Box type %d moving on to sorting\n", this_box);
        break;

    case STATE_BOX_PRE_PUSH:
        if ((this_box == 1 && svt->DI[DI_DIFFUSE_05] == TRUE) ||
            (this_box == 2 && svt->DI[DI_DIFFUSE_06] == TRUE) ||
            (this_box == 3 && svt->DI[DI_DIFFUSE_07] == TRUE)) {
            /* one of the boxes is at the correct pusher */
            printf ("Box type %d at Pusher\n", this_box);
            svt->current_state = STATE_BOX_AT_PUSHER;
        }
        break;

    case STATE_BOX_AT_PUSHER:
        if (this_box == 1)
            svt->DO[DO_PUSHER_01] = TRUE;
        else if (this_box == 2)
            svt->DO[DO_PUSHER_02] = TRUE;
        else if (this_box == 3)
            svt->DO[DO_PUSHER_03] = TRUE;

        svt->current_state=STATE_BOX_PUSHING;

        /* go ahead and raise the second gate */
        // svt->DO[DO_STOP_BLADE_02]=TRUE;
        break;

    case STATE_BOX_PUSHING:
        /* if we've reached the front limit switch, pull back and move on*/
        if (this_box == 1 && svt->DI[DI_PUSH_FR_LIMIT_01] == TRUE) {
            //svt->DO[DO_PUSHER_01] = FALSE;
            svt->current_state = STATE_BOX_PUSHED;
        }
        if (this_box == 2 && svt->DI[DI_PUSH_FR_LIMIT_02] == TRUE) {
            //svt->DO[DO_PUSHER_02] = FALSE;
            svt->current_state = STATE_BOX_PUSHED;
        }
        if (this_box == 3 && svt->DI[DI_PUSH_FR_LIMIT_03] == TRUE) {
            // svt->DO[DO_PUSHER_03] = FALSE;
            svt->current_state = STATE_BOX_PUSHED;
        }
        break;

    case STATE_BOX_PUSHED:
        printf("Done pushing box type %d\n",this_box);
        svt->current_state=STATE_WAITING_FOR_BOX_AT_CHUTE;
        break;

    case STATE_WAITING_FOR_BOX_AT_CHUTE:
        if (svt->DI[DI_RETROREF_NOT_02] == FALSE) {
            printf("Box detected at chute\n");
            svt->current_state=STATE_BOX_SORTED;

            /* send the pushers back home */
            svt->DO[DO_PUSHER_01] = FALSE;
            svt->DO[DO_PUSHER_02] = FALSE;
            svt->DO[DO_PUSHER_03] = FALSE;
        }
        break;

    case STATE_BOX_SORTED:
        /*
         * set everything back to ready state
         * make sure the pushers are back, stop blades are up,
         * emitter off, etc
         */
        svt->DO[DO_STOP_BLADE_01] = TRUE;
        svt->DO[DO_STOP_BLADE_02] = TRUE;
        svt->DO[DO_PUSHER_01] = FALSE;
        svt->DO[DO_PUSHER_02] = FALSE;
        svt->DO[DO_PUSHER_03] = FALSE;

        svt->DO[DO_EMITTER_01] = FALSE;
        svt->DO[DO_WARNING_LIGHT_01] = FALSE;
        plc_state_write (plc, svt);

        /* now we go back to ready state */
        printf ("Box is sorted, going back to ready state\n");
        svt->current_state = STATE_READY;
        break;

    case STATE_ERROR:
        svt->DO[DO_WARNING_LIGHT_01] = TRUE;
        svt->DO[DO_EMITTER_01] = FALSE;
        if (svt->DI[DI_RESET_BUTTON_01] == TRUE) {
            /* reset the sorter into a ready configuration */
            sorter_reset (plc, svt);
            svt->current_state = STATE_READY;
        }
        break;

    default:
        break;
    }

    /* check for box in end chute -- immediate error */
    if (svt->DI[DI_DIFFUSE_01] == TRUE) {
        svt->current_state = STATE_ERROR;
        printf ("detected box in end chute\n");
    }

    /* update and sleep until next loop period */
    loop_count++;
    plc_state_write (plc, svt);
    plc_state_update (plc); /* plc_state_update() synchronizes the
                             * SVT with the remote IO device(s) */
    usleep (LOOP_PERIOD_USEC);
    }

    plc_shutdown (plc);
    return 0;
}

void sorter_reset (plc_t *plc, plc_state_t *svt) 
{
    /* reset the sorter outputs to the default */
    plc_state_update (plc);   /* sync with remote device */
    plc_state_read (plc,svt); /* read state of remote IO */

    /* raise stop blades */
    svt->DO[DO_STOP_BLADE_01] =
    svt->DO[DO_STOP_BLADE_02] = TRUE;

    /* make sure pushers are in rest position */
    svt->DO[DO_PUSHER_01] =
    svt->DO[DO_PUSHER_02] =
    svt->DO[DO_PUSHER_03] = FALSE;

    /* turn emitter off */
    svt->DO[DO_EMITTER_01] = FALSE;

    /* turn warning light off */
    svt->DO[DO_WARNING_LIGHT_01] = FALSE;

    /* now update svt, it will be synced at end of cycle */
    plc_state_write (plc, svt);
}
