//
// es_vm
//

#ifndef ES_VM_H
#define ES_VM_H

#include <stdint.h>

// Never change E_INSTR_BYTES!
#define    E_INSTR_BYTES            ((uint32_t)9)
#define    E_INSTR_SINGLE_BYTES    ((uint32_t)1)

// You may change these values (carefully!)
#define E_STACK_SIZE        ((uint32_t)128)
#define E_MAX_GLOBALS		((uint32_t)32)
#define E_MAX_LOCALS		((uint32_t)16)
#define E_OUT_DS_SIZE       ((int)2500)    // FIXME

#define E_MAX_STRLEN    ((int)128)
#define E_MAX_ARRAYSIZE ((int)512)
#define E_MAX_ARRAYS    ((int)8)
#define E_MAX_CALLFRAMES ((int)32)

// Defines external C-API linkage
#define E_MAX_EXTIDENTIFIERS    ((int)32)
#define E_MAX_EXTIDENTIFIERS_STRLEN ((int)64)

typedef enum {
	E_ARGT_NUMBER = 0,
	E_ARGT_STRING = 1
} e_arg_type;

typedef enum {
	E_STATUS_UNDERFLOW = -6,
	E_STATUS_NESIZE = -2,
	E_STATUS_NOINIT = -1,
	E_STATUS_OK = 1,
} e_statusc;

typedef enum {
	E_VM_STATUS_ERROR = -1,
	E_VM_STATUS_READY = 0,
	E_VM_STATUS_OK = 1,
	E_VM_STATUS_EOF = 2
} e_vm_status;

// ES Types
typedef struct {
	uint8_t sval[E_MAX_STRLEN];
	uint32_t slen;
} e_str_type;

typedef struct {
	uint32_t aptr;
	uint32_t alen;
} e_array_type;

typedef struct {
	union {
		double val;
		e_str_type sval;
		e_array_type aval;
	};
	enum {
		E_NUMBER = 10, E_STRING = 20, E_ARRAY = 30
	} argtype;
} e_value;

// Array type
typedef struct array_entry {
	e_value v;
	uint8_t used;
} e_array_entry;

// Stack
typedef struct {
	e_value entries[E_STACK_SIZE];
	uint32_t size;
	uint32_t top;
} e_stack;

typedef struct {
	e_statusc status;
	e_value val;
} e_stack_status_ret;

typedef struct {
	double retAddr;
	e_stack locals;
} e_callframe;

// VM
typedef struct {
	uint32_t ip;
	e_stack stack;
	e_value globals[E_MAX_GLOBALS];
	e_value locals[E_MAX_LOCALS];
	e_callframe callframes[E_MAX_CALLFRAMES];
	uint32_t cfcnt;
	e_vm_status status;

	uint8_t ds[E_OUT_DS_SIZE];
	uint32_t dscnt;

	uint8_t pupo_is_data;
	int32_t pupo_arr_index;
	e_array_entry arrays[E_MAX_ARRAYS][E_MAX_ARRAYSIZE];
	uint32_t acnt;
} e_vm;

// External subroutines / functions
typedef struct {
	char identifier[E_MAX_EXTIDENTIFIERS_STRLEN];

	uint32_t (*fptr)(e_vm *vm, uint32_t arglen);
} e_external_mapping;

// map["my_external_func"] = &my_func_ptr;
// TODO: This should be replaced by a decent hash map!
static e_external_mapping e_external_map[E_MAX_EXTIDENTIFIERS];

// OPCODES
typedef enum {
	E_OP_NOP = 0x00,       /* NOP																		    */
	E_OP_PUSHG = 0x10,     /* Push global variable,                    PUSHG [index],      s[-1]           	*/
	E_OP_POPG = 0x11,      /* Pop global variable,                     POPG [index]        [s-1]         	*/
	E_OP_PUSHL = 0x12,     /* Push local variable,                     PUSHL [index]                       	*/
	E_OP_POPL = 0x13,      /* Pop local variable,                      POPG [index]        [s-1]         	*/
	E_OP_PUSH = 0x14,      /* Push variable onto top of stack,         PUSH 3                              	*/
	E_OP_PUSHS = 0x15,     /* Push string 							   PUSHS [ascii byte(s)] 				*/
	E_OP_DATA = 0x16,      /* Size of following data segment,		   DATA [entries]	   s[-entries]		*/
	E_OP_PUSHA = 0x17,     /* Push index of followed array access,	   PUSHA [index]						*/
	E_OP_PUSHAS = 0x18,    /* Push index of followed array from stack, PUSHAS 								*/

	E_OP_EQ = 0x20,        /* Equal check,                             EQ,                 s[-1]==s[-2]    	*/
	E_OP_LT = 0x21,        /* Less than,                               LT,                 s[-1]<s[-2]     	*/
	E_OP_GT = 0x22,        /* Greater than,                            GT,                 s[-1]<s[-2]    	*/
	E_OP_LTEQ = 0x23,      /* Less than or equal,                      LTEQ,               s[-1]<=s[-2]    	*/
	E_OP_GTEQ = 0x24,      /* Greater than or equal,                   GTEQ,               s[-1]>=s[-2]    	*/
	E_OP_NOTEQ = 0x25,     /* Not equal check,						   NOTEQ,			   s[-1]!=[s-2]		*/

	E_OP_ADD = 0x30,
	E_OP_NEG = 0x31,
	E_OP_SUB = 0x32,
	E_OP_MUL = 0x33,
	E_OP_DIV = 0x34,
	E_OP_AND = 0x35,
	E_OP_OR = 0x36,
	E_OP_NOT = 0x37,
	E_OP_CONCAT = 0x38,    /* Concatenate strings                       CONCAT              s[s-1].[s-2]   */
	E_OP_MOD = 0x39,       /* Modulo                                    MOD                 s[-1] % s[-2]  */

	E_OP_JZ = 0x40,        /* Jump if zero,                            JZ [addr]                           */
	E_OP_JMP = 0x41,       /* unconditional jump,                      JMP [addr]                          */
	E_OP_JFS = 0x42,       /* Jump from stack value, 				   JFS s[s-1]						   */
	E_OP_JMPFUN = 0x43,    /* unconditional jump to function,		   JMPFUN [addr]					   */
	E_OP_CALL = 0x44,      /* Calls an external defined subroutine	   CALL s[s-1]						   */

	E_OP_PRINT = 0x50,     /* Print statement (debug)                  PRINT(expr)                         */
	E_OP_ARGTYPE = 0x51,   /* Argtype statement 					   ARGTYPE(expr)					   */
	E_OP_LEN = 0x52,       /* Len statement							   LEN(expr)						   */
	E_OP_ARRAY = 0x53, 	   /* Array (dim) statement					   ARRAY(n)							   */
} e_opcode;

/* Single byte operations
   (operations without any argument)
    E_OP_NOP
   	E_OP_PUSHAS
	E_OP_EQ
	E_OP_LT
	E_OP_GT
	E_OP_LTEQ
	E_OP_GTEQ
	E_OP_NOTEQ
	E_OP_ADD
	E_OP_NEG
	E_OP_SUB
	E_OP_MUL
	E_OP_DIV
	E_OP_AND
	E_OP_OR
	E_OP_NOT
	E_OP_MOD
	E_OP_PRINT
	E_OP_ARGTYPE
	E_OP_LEN */
static const uint8_t sb_ops[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
								 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1,
								 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1};

typedef struct {
	e_opcode OP;
	uint32_t op1;
	uint32_t op2;
} e_instr;

// VM
void e_vm_init(e_vm *vm);
e_vm_status e_vm_parse_bytes(e_vm *vm, const uint8_t bytes[], uint32_t blen);
e_vm_status e_vm_evaluate_instr(e_vm *vm, e_instr instr);
e_value e_create_number(double n);
e_value e_create_string(const char *str);
e_value e_create_array(e_vm *vm, e_value *arr, uint32_t arrlen);

// API
e_stack_status_ret e_api_stack_push(e_stack *stack, e_value v);
e_stack_status_ret e_api_stack_pop(e_stack *stack);
void e_api_register_sub(const char *identifier, uint32_t (*fptr)(e_vm *, uint32_t));
uint8_t e_api_call_sub(e_vm *vm, const char *identifier, uint32_t arglen);

#endif //ES_VM_H