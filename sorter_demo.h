#ifndef SORTER_DEMO_H_
#define SORTER_DEMO_H_

/* these enums represent the devices attached to the digital inputs (DI) and outputs (DO) set up in the virtual factory. The virtual factory and
 * these definitions have to match
*/
enum {
	DO_BELT_CONVEYOR_01=0,	// source conveyor belt
	DO_BELT_CONVEYOR_02,	// downstream conveyor belt
	DO_STOP_BLADE_01,		// stop blade between conveyors
	DO_PIVOT_ARM_TURN_01,	// first sorting arm move into position
	DO_PIVOT_ARM_BELT_01,	// first sorting arm vertical belt
	DO_PIVOT_ARM_TURN_02,	// second sorting arm move into position
	DO_PIVOT_ARM_BELT_02,	// second sorting arm vertical belt
//	DO_PIVOT_ARM_TURN_03,	// third sorting arm move into position
//	DO_PIVOT_ARM_BELT_03,	// third sorting arm vertical belt
	DO_EMITTER_01,			// turn the virtual box emitter on or off
	DO_EOL					// End of list
};

enum {
	DI_DIFFUSE_01=0,
	DI_DIFFUSE_02,
	DI_DIFFUSE_03,
	DI_DIFFUSE_04,
	DI_RETROREF_NOT_01,
	DI_RETROREF_NOT_02,
	DI_EOL
};

// Define main states
enum {
	STATE_NOT_READY=0,
	STATE_READY,
	STATE_WAITING_FOR_BOX_EXIT_EMITTER,
	STATE_WAITING_FOR_BOX_AT_GATE,
	STATE_BOX_AT_GATE,
	STATE_BOX1_DETECTED,
	STATE_BOX1_SORTING,
	STATE_BOX2_DETECTED,
	STATE_BOX2_SORTING,
	STATE_BOX3_DETECTED,
	STATE_BOX3_SORTING,
	STATE_WAITING_FOR_BOX_AT_CHUTE,
	STATE_BOX_SORTED,
	STATE_ERROR,
	STATE_EOL
};

// Define main states
char *state_strings[]=
{
	"STATE_NOT_READY",
	"STATE_READY",
	"STATE_WAITING_FOR_BOX_EXIT_EMITTER",
	"STATE_WAITING_FOR_BOX_AT_GATE",
	"STATE_BOX_AT_GATE",
	"STATE_BOX1_DETECTED",
	"STATE_BOX1_SORTING",
	"STATE_BOX2_DETECTED",
	"STATE_BOX2_SORTING",
	"STATE_BOX3_DETECTED",
	"STATE_BOX3_SORTING",
	"STATE_WAITING_FOR_BOX_AT_CHUTE",
	"STATE_BOX_SORTED",
	"STATE_ERROR",
	"STATE_EOL"
};

// box types
enum {
	BOX_TYPE_UNKNOWN=0,
	BOX_TYPE_01,
	BOX_TYPE_02,
	BOX_TYPE_03,
	BOX_EOL
};


#endif /* SORTER_DEMO_H_ */
