#include <stdio.h>
#include <stdlib.h>
#include "embedvm.h"


#define MAX_PID 8
#define MAX_SOCK 16
#define MAX_PIPE 8
#define PIPE_BUF 64
#define PROG_SZ 4096
#define KERNEL_VER 402
#define MAX_FS 8
#define MAX_FILE 8
#define UNUSED __attribute__((unused))
int curvm = 0;
uint8_t vm_mem[MAX_PID][PROG_SZ] = { 0, };
struct embedvm_s vmarrx[MAX_PID] = { 0, };
struct embedvm_s *vmarr[MAX_PID];
char hostname[32] = "default";
char vm_pipes[MAX_PID * 2 + MAX_PIPE][PIPE_BUF];
char *vm_args[MAX_PID] = { NULL, };
int16_t vm_pipen[MAX_PID * 2 + MAX_PIPE] = {0,};
int16_t vm_pipex[MAX_PID * 2 + MAX_PIPE] = {0,};
int16_t vm_uid[MAX_PID] = {32767, };
char *vm_files[MAX_FILE] = {NULL, };
int16_t vm_fseek[MAX_FILE] = {0, }; 
int16_t vm_fpipe[MAX_FILE] = {0, };
int16_t vm_pipeu[MAX_PID * 2 + MAX_PIPE] = {0, };
int16_t vm_pipef[MAX_PID * 2 + MAX_PIPE] = {0, };
int freepipe() {
	int q;
	for ( q = MAX_PID * 2; q < MAX_PID * 2 + MAX_PIPE; q++ ) { if(!vm_pipeu[q]) { return q; } }
	return -1;
}
enum fsact {
	LS, RM, WRITE, READ, INFO
};
enum fsact vm_fact[MAX_FILE];
typedef struct fsparsed {
	int driver;
	char path[64];
} fsparsed;
typedef int (*fsdriver)(enum fsact,int,int,char*,char*);

#define newfsdriver(n) int n (enum fsact action, int seekn, int len, char *path, char *buf)
#ifndef NO_FOPEN
newfsdriver(fs_default){
	if(action == LS) {
		char cmd[1024];
		
		#ifdef WIN32
		sprintf(cmd, "dir /b %s >tmp.txt", path);
		#else
		sprintf(cmd, "ls %s >tmp.txt", path);
		#endif
		//printf("ls: %s\n", path);
		path = strdup("tmp.txt");
		system(cmd);
	}
	if(action == READ || action == LS) {
		FILE *q = fopen(path, "rt");
		  fseek (q , 0 , SEEK_END);
  		int sz = ftell (q);
  		rewind (q);
		if ( seekn >= sz ) {
			return -1;
		}
		//printf("path: %s\n", path);
		if ( q ) {
		fseek(q, seekn, SEEK_SET);
		int ret = fread(buf, 1, len, q);
		fclose(q);
		return ret;
		}
	}
	return -1;
}
#endif
fsdriver kfsdrv[MAX_FS] = { fs_default, NULL, };
struct fsparsed parsefs(char *pwd, char *path) {
	char tmppath[256];
	struct fsparsed ret = { 0, NULL };
	if ( path[0] != '/' ) {
		sprintf(tmppath, "%s/%s", pwd, path);
	} else {
		strcpy(tmppath, path);
	}
	if ( !isdigit(tmppath[1]) ) {
		ret.driver = 0; strcpy(ret.path,&tmppath[1]);
		return ret;
	}
	//printf("%s\n", tmppath);
	ret.driver = tmppath[1] - '0';
	strcpy(ret.path, &tmppath[3]);
	return ret;
}
int fscall(enum fsact action, int seekn, int len, char *pwd, char *name, char *buf) {
	fsparsed parsed = parsefs(pwd, name);
	return (*kfsdrv[parsed.driver])(action, seekn, len, parsed.path, buf);
}
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
int freefd() {
	int q = 0;
	for ( q = 0; vm_files[q] != NULL; q++ ) {}
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
	//printf("%d: Syscall: %d ( %d, %d )\n", curvm, funcid, argc > 0 ? argv[0] : -1, argc > 1 ? argv[1] : -1);
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
		return KERNEL_VER;
	} else if ( funcid == 3 ) {
		// let's fork
		int newpid = freepid();
		//printf("FORK: %d\n", newpid);
		struct embedvm_s* nvm = &vmarrx[newpid];
		nvm->ip = vmarr[curvm]->ip + 2;
		nvm->sp = vmarr[curvm]->sp;
		nvm->sfp = vmarr[curvm]->sfp;
		nvm->mem_read = &mem_read;
		nvm->mem_write = &mem_write;
		nvm->call_user = &call_user;
		//printf("FORK! %d\n", newpid);
		
		memcpy(vm_mem[newpid], vm_mem[curvm], PROG_SZ);
		vmarr[newpid] = &vmarrx[newpid];
		vm_uid[newpid] = vm_uid[curvm];
		//printf("FORK DONE\n");
		fflush(stdout);
		return newpid;
	} else if ( funcid == 4 ) {
		return vmarr[argv[0]] == NULL;
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
		vm_args[curvm] = strdup(&vm_mem[curvm][argv[1]]);
		char *str = strdup(&vm_mem[curvm][argv[0]]);
		char *ptr = strchr(str, ' ');
		if ( ptr ) *ptr = '\0';
		FILE *fp = fopen(str, "rb");
		if ( !fp ) {
		char str2[128];
		sprintf(str2, "bin/%s", str);
		fp = fopen(str2, "rb");
		}
		if ( fp ) {
		uint16_t entrypoint = 0;
		fread(&entrypoint, 1, 2, fp);
		fread(vm_mem[curvm], 1, 4096, fp);
		fclose(fp);
		vmarr[curvm]->ip = entrypoint - 4;
		} else {
		free(str);
		return -1;
		}
		free(str);
	} else if ( funcid == 5 ) {
		
		vmarr[curvm] = NULL;
		free(vm_args[curvm]);
	} else if ( funcid == 10 ) {

		char *dest = &vm_mem[curvm][argv[0]];
		if ( vm_args[curvm] != NULL ) {
		strcpy(dest, vm_args[curvm]);
		}
	} else if ( funcid == 12 ) {
		if ( argc > 0 ) {
		switch(argv[0]) {
			case 1: {
				strcpy(&vm_mem[curvm][argv[1]], hostname); break; }
			case 2: {
				if ( vm_uid[curvm] == 1 ) {
				strcpy(hostname, &vm_mem[curvm][argv[1]]); } 
				break; }
			case 3: {
				//printf("Pipe empty: %d\n", vm_pipen[argv[1]]);
				return vm_pipen[argv[1]] == 0; break;
			}
			case 11: {
				vm_pipeu[argv[1]] = 0; 
				vm_pipef[argv[1]] = 0; break;
			}
			case 12: {
				int ret = freepipe();
				vm_pipeu[ret] = 1;
				return ret;
			}
			case 10: {
				return vm_pipen[argv[1]]; break;
			}
			case 19: {
				vm_pipen[argv[1]] = 0; vm_pipex[argv[1]] = 0; break;
			}
			case 4: {
				return vm_uid[argv[1]];
			}
			case 5: {
				if ( vm_uid[curvm] == 1 ) {
				vm_uid[argv[1]] = argv[2];
				}
				break;
			}
			case 6: {
				strcpy(&vm_mem[curvm][argv[1]], __DATE__ " " __TIME__); break;
			}
			case 7: {
				int newfd = -1;
				newfd = freefd();
				vm_files[newfd] = strdup(&vm_mem[curvm][argv[1]]);
				vm_fpipe[newfd] = argv[2];
				vm_fseek[newfd] = 0;
				vm_fact[newfd] = READ;
				vm_pipen[argv[2]] = 0;
				vm_pipex[argv[2]] = 0;
				vm_pipef[argv[2]] = 1;
				break;
			}
			case 21: {
				char *ptr = &vm_mem[curvm][argv[1]];
				ptr[0] = '\0';
				int pq;
				for ( pq = 0; pq < MAX_PID; pq++ ) {
					if (vmarr[pq] != NULL) {
					sprintf(ptr, "%s%d;%d;%s;", ptr, pq, vm_uid[pq], vm_args[pq]);
					}
				}
				break;
			}
			case 14: {
				int newfd = -1;
				newfd = freefd();
				vm_files[newfd] = strdup(&vm_mem[curvm][argv[1]]);
				vm_fpipe[newfd] = argv[2];
				vm_fseek[newfd] = 0;
				vm_fact[newfd] = LS;
				vm_pipen[argv[2]] = 0;
				vm_pipex[argv[2]] = 0;

				vm_pipef[argv[2]] = 1;
				break;
			}
			case 20: {
				return vm_pipef[argv[1]];
				break;
			}
			case 8: {
				int thefd = -1;
				for(thefd = 0; thefd < MAX_FILE; thefd++ ) {
					if ( vm_fpipe[thefd] == argv[1] ) {
						vm_fpipe[thefd] = 0;
						free(vm_files[thefd]);
						vm_files[thefd] = NULL;
						vm_fseek[thefd] = 0;
					}
				}
				break;
			}
			case 9: {
				int thefd = 0;
				for(thefd = 0; thefd < MAX_FILE; thefd++ ) {
					if ( vm_fpipe[thefd] == argv[1] ) {
						return 1;
					}
				}
				return 0;
				break;
			}
		}
		}
	}
	return 0;
}

void setup(int argc, char **argv)
{
	vm_args[0] = strdup(argv[1]);
	int q = 0;
	for ( q = 0; q < MAX_PID; q++ ) {
		vmarr[q] = NULL;
	}
	vmarr[0] = &vmarrx[0];
	setbuf(stdout, NULL);
	uint16_t entrypoint = 0;
	/*FILE *fp = fopen(argv[1], "rb");
	
	fread(&entrypoint, 1, 2, fp);
	fread(vm_mem[0], 1, 4096, fp);
	fclose(fp);*/
	int ret = fscall(READ, 0, 2, "/0", argv[1], &entrypoint);
	if ( ret == -1 ) {
		printf("Kernel Panic! Can't load init.\n");
		while(1) {}
	}
	ret = fscall(READ, 2, PROG_SZ, "/0", argv[1], vm_mem[0]);
	if ( ret == -1 ) {
		printf("Kernel Panic! Can't load init.\n");
		while(1) {}
	}
	struct embedvm_s* vm = vmarr[0];
	vm->ip = entrypoint;
	printf("Ronix kernel %d: loaded init. Jumping to entrypoint: 0x%04x.", KERNEL_VER, entrypoint);
	vm->sp = vm->sfp = sizeof(vm_mem[curvm]);
	vm->mem_read = &mem_read;
	vm->mem_write = &mem_write;
	vm->call_user = &call_user;
	vm_uid[curvm] = 1;
}

void loop()
{
	// Serial.print("<");
	// Serial.print(vm.ip, DEC);
	// Serial.print(">");
	int n = 0;
	int adder = 0;
	for ( n = 0; n < MAX_FILE; n++ ) {
	if ( vm_files[n] != NULL ) {
	//printf("Pipe left: %d\n", vm_pipen[vm_fpipe[n]]);
	if ( vm_pipen[vm_fpipe[n]] == 0 ) {
		vm_pipex[vm_fpipe[n]] = 0;
		adder = fscall(vm_fact[n], vm_fseek[n], PIPE_BUF / 2, "/0", vm_files[n], vm_pipes[vm_fpipe[n]]);
		if ( adder == -1 ) {
		free(vm_files[n]);
		vm_files[n] = 0;
		vm_fseek[n] = 0;
		vm_fpipe[n] = 0;
		} else {
		vm_pipen[vm_fpipe[n]] = adder;
		vm_fseek[n] += adder;
		}
	}
	}
	}
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
