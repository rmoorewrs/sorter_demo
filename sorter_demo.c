/*
* 01/21/16-initial code written, Rich Moore, richard.moore@windriver.com, link with libmodbus v3.0.x
*
* This program controls a virtual conveyor belt sorter implemented in Factory IO. This is one of the default pre-built scenarios in Factory IO
* Communication is via modbus tcp, but the communication layer has been abstracted out by the plc library. The plc library implements a modbus client(master) that
* sends requests to the Factory IO simulation which must be configured as a Modbus TCP Server. The program must be linked with libmodbus 3.0.5 or 3.06
*
* The plc_t structure  contains a state variable table which reflects the state of the remote device IO as of last update via plc_state_update()
*
* No ladder logic function is available so we have to set up output states and define under which input conditions to apply the right output states
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
#include <sorter_demo.h>

#define SERVER_TYPE  18888
#define SERVER_INST  17

#define IP_PORT	502
#define	MB_SLAVE_ID	1
#define LOOP_PERIOD_USEC	50000	// 20Hz is a period of 50ms or 50000usec


// This is all specific to the sorter pre-built demo in values for conveyor belt control
#define DEMO_DIG_INPUTS		6
#define DEMO_DIG_OUTPUTS	8

void sorter_reset(plc_t *plc,plc_state_t *svt);


int main(int argc, char *argv[])
{
	plc_t	*plc;
	plc_state_t *svt;
	int current_state, wait_state;
	unsigned long loop_count=0;
	unsigned long timer0,timer1,timer2;
	int waiting=FALSE;
	int	box_type=BOX_TYPE_UNKNOWN;
	int	this_box=0;
	int connection_type=0;
	char ip_address[80];
	int reset_command=FALSE;

	// read command line args
	if (argc <= 2) {
		fprintf(stderr,"Usage:\n%s m|r <modbus_slave_ip_address> \n", argv[0]);
		fprintf(stderr,"\t\t\t-or-\n");
		fprintf(stderr,"%s t\n",argv[0]);
		fprintf(stderr,"\t\t\tm=modbus tcp protocol\n");
		fprintf(stderr,"\t\t\tr=reset modbus slave\n");
		fprintf(stderr,"\t\t\tt=tipc relay to modbus gateway\n");
		exit (-1);
	} else {
		if (*argv[1]=='m')
			connection_type=PLC_CONNECTION_MODBUS;
		else if (*argv[1]=='r') {
			connection_type=PLC_CONNECTION_MODBUS;
			reset_command=TRUE;
		}
		else if (*argv[1]=='t')
			connection_type=PLC_CONNECTION_TIPC;
		else
			connection_type=PLC_CONNECTION_NONE;
	}
	if (connection_type==PLC_CONNECTION_MODBUS)
	{
		if (argv[2])
			strncpy(ip_address,argv[2],17);
		else {
			fprintf(stderr,"%s: you must specify ip address for modbus or modbus reset",argv[0]);
			exit (-1);
		}
				
	}
	
	// allocate a state variable table
	svt = malloc(sizeof(plc_state_t));

	// initialize the virtual plc
	if (connection_type == PLC_CONNECTION_MODBUS) {
		plc = plc_init_modbus(ip_address, IP_PORT, MB_SLAVE_ID, DEMO_DIG_INPUTS, DEMO_DIG_OUTPUTS, FALSE);
		if (reset_command == TRUE) {
			sorter_reset(plc,svt);
			plc_shutdown(plc);
			exit(0);
		}
	}
	else if (connection_type == PLC_CONNECTION_TIPC)
		plc=plc_init_tipc_client(SERVER_TYPE, SERVER_INST, 10000, DEMO_DIG_INPUTS, DEMO_DIG_OUTPUTS, FALSE);
	
	timer0=timer1=timer2=0;
	current_state=STATE_NOT_READY;

	/* 
	 * This is the main loop, which runs forever at at a fixed period (20ms) 
	 * 
	 */
	
	while (1) {

		// read the latest state variable table, copy into svt
		plc_state_read(plc,svt);

		switch(current_state) {

		case STATE_NOT_READY :
			// reset the sorter into a ready configuration
			sorter_reset(plc,svt);
			// now we should be ready
			current_state=STATE_READY;
			break;

		case STATE_READY :
			printf("%s\n",state_strings[current_state]);
			// we're ready, so turn on box emitter and go to next state
			plc_state_read(plc,svt);
			svt->DO[DO_EMITTER_01]=TRUE;
			plc_state_write(plc,svt); // update will be done at end of loop

			// now wait for box to arrive at first sensor outside of emitter
			current_state=STATE_WAITING_FOR_BOX_EXIT_EMITTER;
			break;

		case STATE_WAITING_FOR_BOX_EXIT_EMITTER :
			// printf("%s\n",state_strings[current_state]);
			// has box arrived at first sensor yet?
			if(svt->DI[DI_DIFFUSE_01]==TRUE) {
				// box is detected by sensor, turn off emitter
				svt->DO[DO_EMITTER_01]=FALSE;
				plc_state_write(plc,svt);
				current_state=STATE_WAITING_FOR_BOX_AT_GATE;
			} else {
				// keep waiting -- probably need to set up a timeout
			}
			break;

		case STATE_WAITING_FOR_BOX_AT_GATE :
			// detecting box at gate
			if (svt->DI[DI_DIFFUSE_03]) {
				printf("Box Detected at Gate\n");
				current_state=STATE_BOX_AT_GATE;
			}

			break;


		case STATE_BOX_AT_GATE :
			// we have to identify the box type

			this_box=0;
			printf("Detecting box type at gate...");

			// make a code byte from 3 sensor values
			this_box =
					(svt->DI[DI_RETROREF_NOT_02] == TRUE ? 0x00 : 0x01 ) +
					(svt->DI[DI_DIFFUSE_02] == TRUE ? 0x10 : 0x00) +
					(svt->DI[DI_DIFFUSE_03] == TRUE ? 0x100 : 0x00);


			switch (this_box) {

			case 0x00 :
				box_type=BOX_TYPE_UNKNOWN;
				break;
			case 0x0111 :
				box_type=BOX_TYPE_01;
				current_state=STATE_BOX1_DETECTED;
				break;
			case 0x110 :
				box_type=BOX_TYPE_02;
				current_state=STATE_BOX2_DETECTED;
				break;
			case 0x100 :
				box_type=BOX_TYPE_03;
				current_state=STATE_BOX3_DETECTED;
				break;
			default:
				break;
			}

			printf("type %.4x\n",this_box);
			break;

		case STATE_BOX1_DETECTED :
			printf("Detected Box type 1\n;");
			current_state=STATE_BOX1_SORTING;
			break;

		case STATE_BOX1_SORTING :
			// actuate sorter arm and belt 1
			printf("Sorting Box 1\n");
			svt->DO[DO_PIVOT_ARM_TURN_01]=TRUE;
			svt->DO[DO_PIVOT_ARM_BELT_01]=TRUE;
			// lower blade
			svt->DO[DO_STOP_BLADE_01]=FALSE;
			plc_state_write(plc,svt);
			current_state=STATE_WAITING_FOR_BOX_AT_CHUTE;
			break;

			break;

		case STATE_BOX2_DETECTED :
			printf("Detected Box type 2\n");
			current_state=STATE_BOX2_SORTING;
			break;

		case STATE_BOX2_SORTING :
			// actuate sorter arm and belt 2
			printf("Sorting Box 2\n");
			svt->DO[DO_PIVOT_ARM_TURN_02]=TRUE;
			svt->DO[DO_PIVOT_ARM_BELT_02]=TRUE;
			// lower blade
			svt->DO[DO_STOP_BLADE_01]=FALSE;
			plc_state_write(plc,svt);
			current_state=STATE_WAITING_FOR_BOX_AT_CHUTE;
			break;

		case STATE_BOX3_DETECTED :
			printf("Detected Box type 3\n");
			current_state=STATE_BOX3_SORTING;
			break;

		case STATE_BOX3_SORTING :
			// actuate sorter arm and belt 3
			printf("Sorting Box 3\n");
			// svt->DO[DO_PIVOT_ARM_TURN_03]=TRUE;
			// svt->DO[DO_PIVOT_ARM_BELT_03]=TRUE;
			// lower blade
			svt->DO[DO_STOP_BLADE_01]=FALSE;
			plc_state_write(plc,svt);
			current_state=STATE_WAITING_FOR_BOX_AT_CHUTE;
			break;

		case STATE_WAITING_FOR_BOX_AT_CHUTE :
			if (svt->DI[DI_RETROREF_NOT_01]==FALSE || svt->DI[DI_DIFFUSE_04]==TRUE){
				printf("Box detected at chute\n");
				current_state=STATE_BOX_SORTED;
			}

			break;
		case STATE_BOX_SORTED :
			printf("Box is sorted\n");
			// set everything back to ready state
			// put the sorting arms back, turn off the belts
			svt->DO[DO_PIVOT_ARM_TURN_01]=FALSE;
			svt->DO[DO_PIVOT_ARM_BELT_01]=FALSE;
			svt->DO[DO_PIVOT_ARM_TURN_02]=FALSE;
			svt->DO[DO_PIVOT_ARM_BELT_02]=FALSE;
			//svt->DO[DO_PIVOT_ARM_TURN_03]=FALSE;
			//svt->DO[DO_PIVOT_ARM_BELT_03]=FALSE;

			// raise the stop
			svt->DO[DO_STOP_BLADE_01]=TRUE;
			plc_state_write(plc,svt);

			// now we go back to ready state
			current_state=STATE_READY;
			break;

		case STATE_ERROR :
			break;

		default:
			break;
		}



		// update and sleep until next loop period
		loop_count++;
		plc_state_update(plc);		// plc_state_update() synchronizes the SVT with the remote IO device(s)
		usleep(LOOP_PERIOD_USEC);

	}


	plc_shutdown(plc);
	return 0;
}

void sorter_reset(plc_t *plc,plc_state_t *svt) 
{
	// reset the sorter outputs to the default 
	plc_state_update(plc);	 // sync with remote device
	plc_state_read(plc,svt); // read state of remote IO

	// turn on conveyors and raise stop blade
	svt->DO[DO_BELT_CONVEYOR_01]=
	svt->DO[DO_BELT_CONVEYOR_02]=
	svt->DO[DO_STOP_BLADE_01]= TRUE;

	// make sure sorting arms are in rest position
	svt->DO[DO_PIVOT_ARM_TURN_01]=
	svt->DO[DO_PIVOT_ARM_BELT_01]=
	svt->DO[DO_PIVOT_ARM_TURN_02]=
	svt->DO[DO_PIVOT_ARM_BELT_02]=FALSE;
	// svt->DO[DO_PIVOT_ARM_TURN_03]=
	// svt->DO[DO_PIVOT_ARM_BELT_03]=FALSE;

	// turn emitter off
	svt->DO[DO_EMITTER_01]=FALSE;

	// now update svt, it will be synced at end of cycle
	plc_state_write(plc,svt);

}
