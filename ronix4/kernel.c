#include <stdio.h>
#include <stdlib.h>
#include "embedvm.h"


#define MAX_PID 8
#define MAX_SOCK 16
#define MAX_PIPE 8
#define PIPE_BUF 64
#define UNUSED __attribute__((unused))
int curvm = 0;
uint8_t vm_mem[MAX_PID][4*1024] = { 0, };
struct embedvm_s *vmarr[MAX_PID] = { NULL, };
char vm_pipes[MAX_PID * 2 + MAX_PIPE][PIPE_BUF];
int16_t vm_pipen[MAX_PID * 2 + MAX_PIPE] = {0,};
int16_t vm_pipex[MAX_PID * 2 + MAX_PIPE] = {0,};
int16_t mem_read(uint16_t addr, bool is16bit, void *ctx)
{
	if (addr + (is16bit ? 1 : 0) >= sizeof(vm_mem[curvm]))
		return 0;
	if (is16bit)
		return (vm_mem[curvm][addr] << 8) | vm_mem[curvm][addr+1];
	return vm_mem[curvm][addr];
}

void mem_write(uint16_t addr, int16_t value, bool is16bit, void *ctx)
{
	if (addr + (is16bit ? 1 : 0) >= sizeof(vm_mem[curvm]))
		return;
	if (is16bit) {
		vm_mem[curvm][addr] = value >> 8;
		vm_mem[curvm][addr+1] = value;
	} else
		vm_mem[curvm][addr] = value;
}
int freepid() {
	int q = 0;
	for ( q = 0; vmarr[q] != NULL; q++ ) {}
	return q;
}
 void reverse(char *s, int len)
 {
     int i, j;
     char c;
 
     for (i = 0, j = len-1; i<j; i++, j--) {
         c = s[i];
         s[i] = s[j];
         s[j] = c;
     }
 }
int16_t call_user(uint8_t funcid, uint8_t argc, int16_t *argv, void *ctx)
{
	if ( funcid == 0 && argc == 1 ) {
	if ( argv[0] == '`' ) {
	putchar(10);
	} else {
	putchar(argv[0]);
	}
	} else if ( funcid == 1 ) {
		if ( _kbhit() ) {
		return _getche();
		}
	} else if ( funcid == 2 ) {
		char *sysinfodest = &vm_mem[curvm][argv[0]];
		strcpy(sysinfodest, "Ronix");
		return 401;
	} else if ( funcid == 3 ) {
		// let's fork
		int newpid = freepid();
		struct embedvm_s* nvm = (struct embedvm_s*)calloc(1, sizeof(struct embedvm_s));
		nvm->ip = vmarr[curvm]->ip + 2;
		nvm->sp = vmarr[curvm]->sp;
		nvm->sfp = vmarr[curvm]->sfp;
		nvm->mem_read = &mem_read;
		nvm->mem_write = &mem_write;
		nvm->call_user = &call_user;
		//printf("FORK! %d\n", newpid);
		memcpy(vm_mem[newpid], vm_mem[curvm], 4096);
		vmarr[newpid] = nvm;
		return newpid;
	} else if ( funcid == 4 ) {
		if ( vmarr[argv[0]] != NULL ) {
		vmarr[curvm]->ip -= 4;
		}
	} else if ( funcid == 7 ) {
		return curvm;
	} else if ( funcid == 8 ) {
		int16_t len = argv[2];
		int16_t pipe = argv[0];
		char* dest = &vm_mem[curvm][argv[1]];
		
		if ( vm_pipen[pipe] < len ) {
			len = vm_pipen[pipe];
		}
		if ( vm_pipen[pipe] == 0 ) {
			vm_pipex[pipe] = 0;
		}
		// 000abcdef
		//    ^ |
		// ^  
		int idx = vm_pipex[pipe];
		//printf("read<%d,%d>=%.*s\n", idx, vm_pipen[pipe], len, vm_pipes[pipe]+idx );
		char *src = (char*)(vm_pipes[pipe] + idx);
		
		memcpy(dest, src, len);
		vm_pipen[pipe] -= len;
		vm_pipex[pipe] += len;
		//printf("read(%d, [[%.*s]], %d)\n", pipe, len, src, len);
		
		return len;
	} else if ( funcid == 9 ) {
		int16_t ret = 0;
		int16_t len = argv[2];
		int16_t pipe = argv[0];
		if ( (len + vm_pipen[pipe]) > PIPE_BUF ) {
			return -1;
		}
		char* src = &vm_mem[curvm][argv[1]];
		char* dest = (char*)(vm_pipes[pipe] + vm_pipex[pipe] + vm_pipen[pipe]);
		//printf("write(%d, [[%.*s]], %d)\n", pipe, len, src, len);
		memcpy(dest, src, len);
		vm_pipen[pipe] += len;
		
		return ret;
	} else if ( funcid == 6 ) {
		char *str = &vm_mem[curvm][argv[0]];
		FILE *fp = fopen(str, "r");
		uint16_t entrypoint = 0;
		fread(&entrypoint, 1, 2, fp);
		fread(vm_mem[curvm], 1, 4096, fp);
		fclose(fp);
		vmarr[curvm]->ip = entrypoint - 4;
	} else if ( funcid == 5 ) {
		vmarr[curvm] = NULL;
	}
	return 0;
}

void setup(int argc, char **argv)
{
	setbuf(stdout, NULL);
	FILE *fp = fopen(argv[1], "r");
	uint16_t entrypoint = 0;
	fread(&entrypoint, 1, 2, fp);
	fread(vm_mem[0], 1, 4096, fp);
	fclose(fp);
	struct embedvm_s* vm = (struct embedvm_s*)calloc(1, sizeof(struct embedvm_s));
	vm->ip = entrypoint;
	printf("Starting kernel: init entrypoint: 0x%04x\n", entrypoint);
	vm->sp = vm->sfp = sizeof(vm_mem[curvm]);
	vm->mem_read = &mem_read;
	vm->mem_write = &mem_write;
	vm->call_user = &call_user;
	vmarr[0] = vm;
}

void loop()
{
	// Serial.print("<");
	// Serial.print(vm.ip, DEC);
	// Serial.print(">");
	int n = 0;
	for ( n = 0; n < MAX_PID; n++ ) {
	if ( vmarr[n] != NULL ) {
	curvm = n;
	//printf("Doing %d\n", n);
	embedvm_exec(vmarr[n]);
	}
	}
}


int main(int argc, char **argv) {
	setup(argc, argv);
	while(1)
		loop();
}
