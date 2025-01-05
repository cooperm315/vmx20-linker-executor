#include "vmx20.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

typedef struct {
    int32_t insym_size;
    int32_t outsym_size;
    int32_t code_size;
} Header;

typedef struct {
    char name[16];
    int32_t addr;
} Sym;

typedef struct {
    Header hdr;
    Sym* syms;
    int32_t* mem;
    pthread_mutex_t data_lock;
    pthread_mutex_t trace_lock;
} VM;

typedef struct {
    VM* handle;
    uint32_t initialSP;
    int* terminationStatus;
    int32_t trace;
    int pid;
    int pn;
} ThreadArgs;

void* execute_helper(void* args);

void *initVm(int32_t *errorNumber)
{
    printf("INIT VM\n");
    VM* vm = (VM*) malloc(sizeof(VM));

    if (!vm) (*errorNumber) = VMX20_INITIALIZE_FAILURE;
    else (*errorNumber) = VMX20_NORMAL_TERMINATION;

    return vm;
}

int32_t loadExecutableFile(void *handle, char *filename, int32_t *errorNumber)
{
    printf("LOAD EXE\n");
    VM* vm = (VM*) handle;

    FILE* file;
    file = fopen(filename, "r");
    // ensure file opened properly
    if (!file)
    {
        (*errorNumber) = VMX20_FILE_NOT_FOUND;
        return 0;
    }

    // check if file is a valid format
    char* token = strtok(filename, ".");
    token = strtok(NULL, ".");
    if (strcmp(token, "exe") != 0)
    {
        (*errorNumber) = VMX20_FILE_IS_NOT_VALID;
        return 0;
    }

    // read header section
    if (fread(&vm->hdr, sizeof(Header), 1, file) != 1)
    {
        printf("ERROR: Failed to read header info\n");
        exit(1);
    }

    // check for outsymbols
    if (vm->hdr.outsym_size != 0)
    {
        (*errorNumber) = VMX20_FILE_CONTAINS_OUTSYMBOLS;
        return 0;
    }

    // read insymbol section
    vm->syms = (Sym*) malloc(sizeof(Sym) * vm->hdr.insym_size / 5);
    if (!vm->syms) 
    {
        printf("ERROR: Could not allocate symbol struct\n");
        exit(1);
    }
    if (fread(vm->syms, sizeof(Sym), vm->hdr.insym_size / 5, file) != vm->hdr.insym_size / 5)
    {
        printf("ERROR: Could not read in symbol info\n");
        exit(1);
    }

    // read mem section
    vm->mem = (int32_t*) calloc(sizeof(int32_t), 10000000);
    if (!vm->mem) 
    {
        printf("ERROR: Could not allocate memory array\n");
        exit(1);
    }
    if (fread(vm->mem, sizeof(int32_t), vm->hdr.code_size, file) != vm->hdr.code_size)
    {
        printf("ERROR: Could not read in mem\n");
        exit(1);
    }

    fclose(file);

    return 1;
}

int32_t getAddress(void *handle, char *label, uint32_t *outAddr)
{
    printf("GET ADDR\n");
    VM* vm = (VM*) handle;

    for (int i = 0; i < vm->hdr.insym_size / 5; i++)
    {
        // address found
        if (strcmp(label, vm->syms[i].name) == 0)
        {
            (*outAddr) = vm->syms[i].addr;
            return 1;
        }
    }

    return 0;
}

int32_t getWord(void *handle, uint32_t addr, int32_t *outWord)
{
    printf("GET WORD\n");
    VM* vm = (VM*) handle;

    if (addr >= vm->hdr.code_size)
        return 0;

    (*outWord) = vm->mem[addr];

    return 1;
}

int32_t putWord(void *handle, uint32_t addr, int32_t word)
{
    printf("PUT WORD\n");
    VM* vm = (VM*) handle;

    if (addr >= vm->hdr.code_size)
        return 0;

    vm->mem[addr] = word;

    return 1;
}

int32_t execute(void *handle, uint32_t numProcessors, uint32_t initialSP[],
      int terminationStatus[], int32_t trace)
{
    printf("EXE\n");
    VM* vm = (VM*) handle;

    pthread_mutex_init(&vm->data_lock, NULL);
    pthread_mutex_init(&vm->trace_lock, NULL);

    pthread_t threads[numProcessors];
    ThreadArgs threadArgs[numProcessors];
    for (int i = 0; i < numProcessors; i++) {
        threadArgs[i].handle = vm;
        threadArgs[i].initialSP = initialSP[i];
        threadArgs[i].terminationStatus = &terminationStatus[i];
        threadArgs[i].trace = trace;
        threadArgs[i].pid = i;
        threadArgs[i].pn = numProcessors;

        pthread_create(&threads[i], NULL, &execute_helper, &threadArgs[i]);
    }

    for (int i = 0; i < numProcessors; i++)
        pthread_join(threads[i], NULL);

    for (int i = 0; i < numProcessors; i++)
    {
        if ((*threadArgs[i].terminationStatus) != VMX20_NORMAL_TERMINATION)
            return 0;
    }

    return 1;
}

void* execute_helper(void* args)
{
    printf("EXE HELP\n");
    ThreadArgs* targs = (ThreadArgs*) args;
    VM* vm = targs->handle;

    int32_t regs[16] = 
    {
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    regs[13] = 0;
    regs[14] = targs->initialSP;
    regs[15] = 0;

    while (regs[15] <= vm->hdr.code_size)
    {
        regs[15]++;
        if (targs->trace == 1) 
        {
            pthread_mutex_lock(&vm->trace_lock);
            char* buffer = (char*) calloc(200, sizeof(char));
            char* buffer1 = (char*) calloc(150, sizeof(char));
            sprintf(buffer1, "<%d> %08x %08x %08x %08x %08x %08x %08x %08x\n%08x %08x %08x %08x %08x %08x %08x %08x\n", targs->pid, regs[0], regs[1], regs[2], 
            regs[3], regs[4], regs[5], regs[6], regs[7], regs[8], regs[9], regs[10], 
            regs[11], regs[12], regs[13], regs[14], regs[15]);
            int errNum = 0;
            disassemble(vm, regs[15] - 1, buffer, &errNum);
            strcat(buffer, buffer1);
            printf("%s", buffer);
            free(buffer);
            pthread_mutex_unlock(&vm->trace_lock);
        }
        int op = vm->mem[regs[15] - 1] & 0xff;
        int r1 = 0;
        int r2 = 0;
        float r1f = 0;
        float r2f = 0;
        float eqf = 0;
        int addr = 0;
        int cons = 0;
        int offset = 0;
        switch(op)
        {
            case 0:     // halt
                (*targs->terminationStatus) = VMX20_NORMAL_TERMINATION;
                return targs; 
                break;
            case 1:     // load
                addr = (vm->mem[regs[15] - 1] & 0xfffff000) >> 12;
                if (addr & (1 << 19))
                    addr |= 0xfff00000;
                if (addr + regs[15] > vm->hdr.code_size)
                {
                    (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                    return targs;
                }
                pthread_mutex_lock(&vm->data_lock);
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = vm->mem[addr + regs[15]];
                pthread_mutex_unlock(&vm->data_lock);
                break;
            case 2:     // store
                addr = (vm->mem[regs[15] - 1] & 0xfffff000) >> 12;
                if (addr & (1 << 19))
                    addr |= 0xfff00000;
                if (addr + regs[15] > vm->hdr.code_size)
                {
                    (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                    return targs;
                }
                pthread_mutex_lock(&vm->data_lock);
                vm->mem[addr + regs[15]] = regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                pthread_mutex_unlock(&vm->data_lock);
                break;
            case 3:     // ldimm
                cons = (vm->mem[regs[15] - 1] & 0xfffff000) >> 12;
                if (cons & (1 << 19)) 
                    cons |= 0xfff00000;
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = cons;
                break;
            case 4:     // ldaddr
                addr = (vm->mem[regs[15] - 1] & 0xfffff000) >> 12;
                if (addr & (1 << 19))
                    addr |= 0xfff00000;
                if (addr + regs[15] > vm->hdr.code_size)
                {
                    (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                    return targs;
                }
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = addr + regs[15];
                break;
            case 5:     // ldind
                offset = (vm->mem[regs[15] - 1] & 0xffff0000) >> 16;
                if (offset & (1 << 15))
                    offset |= 0xffff0000;
                // if (offset + regs[15] > vm->hdr.code_size)
                // {
                //     (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                //     return targs;
                // }
                pthread_mutex_lock(&vm->data_lock);
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = vm->mem[offset + regs[(vm->mem[regs[15] - 1] >> 12) & 0xf]];
                pthread_mutex_unlock(&vm->data_lock);
                break;
            case 6:     // stind
                offset = (vm->mem[regs[15] - 1] & 0xffff0000) >> 16;
                if (offset & (1 << 15))
                    offset |= 0xffff0000;
                // if (offset + regs[15] > vm->hdr.code_size)
                // {
                //     (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                //     return targs;
                // }
                pthread_mutex_lock(&vm->data_lock);
                vm->mem[offset + regs[(vm->mem[regs[15] - 1] >> 12) & 0xf]] = regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                pthread_mutex_unlock(&vm->data_lock);
                break;
            case 7:     // addf
                r1f = *(float*) &regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2f = *(float*) &regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                eqf = r1f + r2f;
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = *(int32_t*) &eqf;
                break;
            case 8:     // subf
                r1f = *(float*) &regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2f = *(float*) &regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                eqf = r1f - r2f;
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = *(int32_t*) &eqf;
                break;
            case 9:     // divf
                r1f = *(float*) &regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2f = *(float*) &regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                if (r2 == 0)
                {
                    (*targs->terminationStatus) = VMX20_DIVIDE_BY_ZERO;
                    return targs;
                }
                eqf = r1f / r2f;
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = *(int32_t*) &eqf;
                break;
            case 10:    // mulf
                r1f = *(float*) &regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2f = *(float*) &regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                eqf = r1f * r2f;
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = *(int32_t*) &eqf;
                break;
            case 11:    // addi
                r1 = regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2 = regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = r1 + r2;
                break;
            case 12:    // subi
                r1 = regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2 = regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = r1 - r2;
                break;
            case 13:    // divi
                r1 = regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2 = regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                if (r2 == 0)
                {
                    (*targs->terminationStatus) = VMX20_DIVIDE_BY_ZERO;
                    return targs;
                }
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = r1 / r2;
                break;
            case 14:    // muli
                r1 = regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2 = regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = r1 * r2;
                break;
            case 15:    // call
                regs[14]--;
                vm->mem[regs[14]] = regs[15];
                regs[14]--;
                vm->mem[regs[14]] = regs[13];
                regs[13] = regs[14];
                regs[14]--;
                vm->mem[regs[14] - 1] = 0;
                addr = (vm->mem[regs[15] - 1] >> 12) & 0xfffff;
                if (addr & (1 << 19))
                    addr |= 0xfff00000;
                if (addr + regs[15] > vm->hdr.code_size)
                {
                    (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                    return targs;
                }
                regs[15] = addr + regs[15];
                break;
            case 16:    // ret
                regs[13] = vm->mem[regs[14] + 1];
                regs[14]++;
                regs[15] = vm->mem[regs[14] + 1];
                regs[14]++;
                vm->mem[regs[13] - 1] = vm->mem[regs[14] - 2];
                regs[14]++;
                break;
            case 17:    // blt
                r1 = regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2 = regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                addr = (vm->mem[regs[15] - 1] >> 16) & 0xffff;
                if (addr & (1 << 15))
                    addr |= 0xffff0000;
                if (addr + regs[15] > vm->hdr.code_size)
                {
                    (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                    return targs;
                }
                if (r1 < r2)
                    regs[15] = addr + regs[15];
                break;
            case 18:    // bgt
                r1 = regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2 = regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                addr = (vm->mem[regs[15] - 1] >> 16) & 0xffff;
                if (addr & (1 << 15))
                    addr |= 0xffff0000;
                if (addr + regs[15] > vm->hdr.code_size)
                {
                    (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                    return targs;
                }
                if (r1 > r2)
                    regs[15] = addr + regs[15];
                break;
            case 19:    // beq
                r1 = regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2 = regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                addr = (vm->mem[regs[15] - 1] >> 16) & 0xffff;
                if (addr & (1 << 15))
                    addr |= 0xffff0000;
                if (addr + regs[15] > vm->hdr.code_size)
                {
                    (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                    return targs;
                }
                if (r1 == r2)
                    regs[15] = addr + regs[15];
                break;
            case 20:    // jmp
                addr = (vm->mem[regs[15] - 1] >> 12) & 0xfffff;
                if (addr << 19 == 1)
                    addr |= 0xfff00000;
                if (addr + regs[15] > vm->hdr.code_size)
                {
                    (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                    return targs;
                }
                regs[15] = addr + regs[15];
                break;
            case 21:    // cmpxchg
                r1 = regs[(vm->mem[regs[15] - 1] >> 8) & 0xf];
                r2 = regs[(vm->mem[regs[15] - 1] >> 12) & 0xf];
                addr = (vm->mem[regs[15] - 1] >> 16) & 0xffff;
                if (addr & (1 << 15))
                    addr |= 0xffff0000;
                if (addr + regs[15] > vm->hdr.code_size)
                {
                    (*targs->terminationStatus) = VMX20_ADDRESS_OUT_OF_RANGE;
                    return targs;
                }
                pthread_mutex_lock(&vm->data_lock);
                if (r1 == vm->mem[addr + regs[15]])
                    vm->mem[addr + regs[15]] = r2;
                else 
                    regs[(vm->mem[regs[15] - 1] >> 8) & 0xf] = vm->mem[addr + regs[15]];
                pthread_mutex_unlock(&vm->data_lock);
                break;
            case 22:    // getpid
                regs[vm->mem[regs[15] - 1] >> 8 & 0xfffff] = targs->pid;
                break;
            case 23:    // getpn
                regs[vm->mem[regs[15] - 1] >> 8 & 0xfffff] = targs->pn;
                break;
            case 24:    // push
                regs[14]--;
                vm->mem[regs[14]] = regs[vm->mem[regs[15] - 1] >> 8 & 0xf];
                
                break; 
            case 25:    // pop
                regs[vm->mem[regs[15] - 1] >> 8 & 0xf] = vm->mem[regs[14]];
                regs[14]++;
                break;
            default: 
                (*targs->terminationStatus) = VMX20_ILLEGAL_INSTRUCTION;
                return targs;
                break;
        }
    }

    (*targs->terminationStatus) = VMX20_NORMAL_TERMINATION;
    return targs;
}

int disassemble(void *handle, uint32_t address, char *buffer, int32_t *errorNumber)
{
    VM* vm = (VM*) handle;
    // if (address > vm->hdr.code_size) 
    // {
    //     (*errorNumber) = VMX20_ADDRESS_OUT_OF_RANGE;
    //     return 0;
    // }
    int op = vm->mem[address] & 0xff;
    int addr = 0;
    int cons = 0;
    int offset = 0;
    int r1 = 0;
    int r2 = 0;
    // char regs[144];
    // TODO: Figure out how to get register values and threadID here
    switch(op)
    {
        case 0:     // halt
            (*errorNumber) = VMX20_NORMAL_TERMINATION;
            buffer = "halt";
            return 1; 
            break;
        case 1:     // load
            addr = (vm->mem[address] & 0xfffff000) >> 12;
            if (addr & (1 << 19))
                addr |= 0xfff00000;
            r1 = (vm->mem[address] >> 8) & 0xf;
            sprintf(buffer, "load r%d, %d\n", r1, addr);
            break;
        case 2:     // store
            addr = (vm->mem[address] & 0xfffff000) >> 12;
            if (addr & (1 << 19))
                addr |= 0xfff00000;
            r1 = (vm->mem[address] >> 8) & 0xf;
            sprintf(buffer, "store r%d, %d\n", r1, addr);
            break;
        case 3:     // ldimm
            cons = (vm->mem[address] & 0xfffff000) >> 12;
            r1 = (vm->mem[address] >> 8) & 0xf;
            sprintf(buffer, "ldimm r%d, %d\n", r1, cons);
            break;
        case 4:     // ldaddr
            addr = (vm->mem[address] & 0xfffff000) >> 12;
            if (addr & (1 << 19))
                addr |= 0xfff00000;
            r1 = (vm->mem[address] >> 8) & 0xf;
            sprintf(buffer, "ldaddr r%d, %d\n", r1, addr);
            break;
        case 5:     // ldind
            offset = (vm->mem[address] & 0xffff0000) >> 16;
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            if (offset & (1 << 15))
                offset |= 0xffff0000;
            sprintf(buffer, "ldind r%d, %d(r%d)\n", r1, offset, r2);
            break;
        case 6:     // stind
            offset = (vm->mem[address] & 0xffff0000) >> 16;
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            if (offset & (1 << 15))
                offset |= 0xffff0000;
            sprintf(buffer, "stind r%d, %d(r%d)\n", r1, offset, r2);
            break;
        case 7:     // addf
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            sprintf(buffer, "addf r%d, r%d\n", r1, r2);
            break;
        case 8:     // subf
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            sprintf(buffer, "subf r%d, r%d\n", r1, r2);
            break;
        case 9:     // divf
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            sprintf(buffer, "divf r%d, r%d\n", r1, r2);
            break;
        case 10:    // mulf
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            sprintf(buffer, "mulf r%d, r%d\n", r1, r2);
            break;
        case 11:    // addi
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            sprintf(buffer, "addi r%d, r%d\n", r1, r2);
            break;
        case 12:    // subi
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            sprintf(buffer, "subi r%d, r%d\n", r1, r2);
            break;
        case 13:    // divi
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            sprintf(buffer, "divi r%d, r%d\n", r1, r2);
            break;
        case 14:    // muli
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            sprintf(buffer, "muli r%d, r%d\n", r1, r2);
            break;
        case 15:    // call
            addr = (vm->mem[address] & 0xfffff000) >> 12;
            if (addr & (1 << 19))
                addr |= 0xfff00000;
            sprintf(buffer, "call %d\n", addr);
            break;
        case 16:    // ret
            buffer = "ret";
            break;
        case 17:    // blt
            addr = (vm->mem[address] & 0xffff0000) >> 16;
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            if (addr & (1 << 15))
                addr |= 0xffff0000;
            sprintf(buffer, "blt r%d, r%d, %d\n", r1, r2, addr);
            break;
        case 18:    // bgt
            addr = (vm->mem[address] & 0xffff0000) >> 16;
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            if (addr & (1 << 15))
                addr |= 0xffff0000;
            sprintf(buffer, "bgt r%d, r%d, %d\n", r1, r2, addr);
            break;
        case 19:    // beq
            addr = (vm->mem[address] & 0xffff0000) >> 16;
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            if (addr & (1 << 15))
                addr |= 0xffff0000;
            sprintf(buffer, "beq r%d, r%d, %d\n", r1, r2, addr);
            break;
        case 20:    // jmp
            addr = (vm->mem[address] & 0xfffff000) >> 12;
            if (addr & (1 << 19))
                addr |= 0xfff00000;
            sprintf(buffer, "jmp %d\n", addr);
            break;
        case 21:    // cmpxchg
            addr = (vm->mem[address] & 0xffff0000) >> 16;
            r1 = (vm->mem[address] >> 8) & 0xf;
            r2 = (vm->mem[address] >> 12) & 0xf;
            if (addr & (1 << 15))
                addr |= 0xffff0000;
            sprintf(buffer, "cmpxchg r%d, r%d, %d\n", r1, r2, addr);
            break;
        case 22:    // getpid
            r1 = vm->mem[address] >> 12 & 0xfffff;
            sprintf(buffer, "getpid r%d\n", r1);
            break;
        case 23:    // getpn
            r1 = vm->mem[address] >> 12 & 0xfffff;
            sprintf(buffer, "getpn r%d\n", r1);
            break;
        case 24:    // push
            r1 = vm->mem[address] >> 8 & 0xf;
            sprintf(buffer, "push r%d\n", r1);
            break; 
        case 25:    // pop
            r1 = vm->mem[address] >> 8 & 0xf;
            sprintf(buffer, "pop r%d\n", r1);
            break;
        default: 
            (*errorNumber) = VMX20_ILLEGAL_INSTRUCTION;
            return 0;
            break;
    }

    return 1;
}

void cleanup(void *handle)
{
    VM* vm = (VM*) handle;
    free(vm->syms);
    free(vm->mem);
    pthread_mutex_destroy(&vm->data_lock);
    pthread_mutex_destroy(&vm->trace_lock);
    free(vm);
}
