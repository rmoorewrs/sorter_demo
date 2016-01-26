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
#include <modbus.h>
#include <vplc.h>
#include <sorter_demo.h>

#define	IP_ADDRESS "172.25.44.47"
#define IP_PORT	502
#define	MB_SLAVE_ID	1
#define LOOP_PERIOD_USEC	50000	// 20Hz is a period of 50ms or 50000usec

#define SERVER_TYPE  18888
#define SERVER_INST  17


// This is all specific to the sorter pre-built demo in values for conveyor belt control
#define DEMO_DIG_INPUTS		5
#define DEMO_DIG_OUTPUTS	10





int main(void)
{
	plc_t	*plc;
	plc_state_t *svt;
	int current_state, wait_state;
	unsigned long loop_count=0;
	unsigned long timer0,timer1,timer2;
	int waiting=FALSE;
	int	box_type=BOX_TYPE_UNKNOWN;
	int	this_box=0;;

	// allocate a state variable table
	svt = malloc(sizeof(plc_state_t));

	// initialize the tipc-modbus gateway server.. the init routine never returns
	plc=plc_tipc_modbus_server(SERVER_TYPE, SERVER_INST, IP_ADDRESS, IP_PORT, MB_SLAVE_ID, DEMO_DIG_INPUTS, DEMO_DIG_OUTPUTS, FALSE);
	
	return 0;
}

