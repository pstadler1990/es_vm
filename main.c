#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "vm.h"
#include "vm_builtins.h"

e_vm context;
#define MAX_BUF_SIZE	((uint32_t)2500)

int main(int argc, char** argv) {
	uint8_t bytes_in[MAX_BUF_SIZE];
	uint32_t bCnt = 0;

	bool bytes_mode = false;
	for(int a = 0; a < argc; a++) {
		if(!bytes_mode) {
			if(strncmp(argv[a], "-b", 2) == 0) {
				bytes_mode = true;
			}
		} else {
			/* Interpret following bytes as in bytes (bytecode stream) */
			if(bCnt < MAX_BUF_SIZE) {
				uint8_t b = (uint8_t)strtod(argv[a], NULL);
				bytes_in[bCnt++] = b;
			}
		}
	}

	e_vm_init(&context);
	e_vm_parse_bytes(&context, bytes_in, bCnt /*sizeof(bytes_in) / sizeof(uint8_t)*/);

	return 0;
}