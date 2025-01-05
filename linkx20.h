#include <stdio.h>
#include <stdlib.h> 
#include <stdint.h> 
#include <string.h>
#include <stdbool.h>

// how large a word is (occupies the same amount of space) 
#define word_t uint32_t

typedef struct {
    word_t insym_size;
    word_t outsym_size;
    word_t code_size;
} Header;

typedef struct {
    char sym_name[16];
    word_t addr;
} Sym;

// Read the header data into a structure
Header read_header(FILE *file);

// Read the symbols (in or out) into an array of a structure
void read_syms(Sym* syms, int num_syms, FILE* file);

// Read the code section into an array of 8 bit (one byte) integers
void read_code(word_t* code, int code_size, FILE* file);

// Ensure the mainx20 function is present in a given file
bool check_mainx20(Sym* syms, Header hdr);

// Put code section of each file into one array
word_t* get_code();

// get a number related to number of args with addrs
int get_args(int op);

// resolve the outsymbols
int res_syms(word_t* code, Sym* exe_insyms, int num_files, int tot_in);

// get all of the insymbols into a 1D array
Sym get_exe_insym(char* insym_name, Sym* exe_insyms, int tot_in);

// add the global address of each insymbol definition
void adjust_insym_addr(int num_files, Sym* exe_insyms);

// create the output .exe file
void create_file(char* file_name, word_t* code, Sym* insyms, int tot_in, int num_files);

// free memory 
void clean_up();