/*
 * vplc.h
 *
 *  Created on: Jan 21, 2016
 *      Author: rmoore
 */

#ifndef VPLC_H_
#define VPLC_H_

#include <modbus.h>
#include <netinet/in.h>
#include <linux/tipc.h>


#define MAX_BUFSIZE	64
#define MAX_INPUTS	16
#define MAX_OUTPUTS	16

enum {
	PLC_STATUS_INIT=0,
	PLC_STATUS_CLEAN,
	PLC_STATUS_DIRTY,
	PLC_STATUS_ERROR
};

// define different connection types
enum {
	PLC_CONNECTION_NONE=0,
	PLC_CONNECTION_MODBUS,
	PLC_CONNECTION_UDP,
	PLC_CONNECTION_TIPC,
	PLC_CONNECTION_TIPC_GATEWAY,
	PLC_CONNECTION_EOL
};

// set up global state variable table
typedef struct _plc_state {
	uint8_t	status;
	uint8_t nDI;
	uint8_t nDO;
	uint8_t	DI[MAX_INPUTS];
	uint8_t	 DO[MAX_OUTPUTS];
} plc_state_t;

typedef struct _plc {
	modbus_t	*mb_slave;
	char net_addr[80];
	int	 net_port;
	int modbus_addr;
	int connection_type;
	int verbose;
	long update_count;
	plc_state_t *state;
	
	// TIPC Client specific 
	int tipc_socket;
	uint32_t tipc_name_type;
	uint32_t tipc_name_instance;
	struct sockaddr_tipc tipc_server_addr;
	struct sockaddr_tipc tipc_client_addr;
} plc_t;


plc_t *plc_init_modbus(char* net_addr, int net_port, int bus_addr, int nDI, int nDO, int debug_flag);
plc_t *plc_init_tipc_client(uint32_t tipc_name_type, uint32_t tipc_name_instance, int tipc_wait, int nDI, int nDO, int debug_flag);
plc_t *plc_tipc_modbus_server(uint32_t tipc_name_type, uint32_t tipc_name_instance, char *net_address, int net_port, int modbus_addr, int nDI, int nDO, int debug_flag);
void plc_state_print(plc_state_t *state, int n_inputs, int n_outputs);
int plc_state_read(plc_t *plc, plc_state_t *state);
int plc_state_write(plc_t *plc, plc_state_t *state);
int plc_state_update(plc_t *plc);
void plc_shutdown(plc_t *plc);
int plc_timer(plc_t *plc,int timer_index);

// tipc client backend functions
void tipc_plc_wait_for_server(__u32 name_type, __u32 name_instance, int wait);

#endif /* VPLC_H_ */
