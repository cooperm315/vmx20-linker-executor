#include "linkx20.h"

Header* hdrs;
Sym** insyms;
Sym** outsyms;
word_t** codes;

int resolved = 0;

int main(int argc, char* argv[])
{
    if (argc < 2) 
    {
        printf("ERROR: Usage ./linkx20 <file1>...<fileN>\n");
        exit(1);
    }

    // allocate memory 
    hdrs = (Header*) malloc(sizeof(Header) * (argc - 1));
    insyms = (Sym**) malloc(sizeof(Sym*) * (argc - 1));
    outsyms = (Sym**) malloc(sizeof(Sym*) * (argc - 1));
    codes = (word_t**) malloc(sizeof(word_t*) * (argc - 1));

    bool has_main = false;

    // read all files
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-o") == 0)
            break;
        FILE* file;
        file = fopen(argv[i], "r");
        if (!file)
        {
            printf("ERROR: Can't open file %s\n", argv[i]);
            exit(1);
        }

        hdrs[i - 1] = read_header(file);
        insyms[i - 1] = (Sym*) malloc(sizeof(Sym) * (hdrs[i - 1].insym_size / 5));
        outsyms[i - 1] = (Sym*) malloc(sizeof(Sym) * (hdrs[i - 1].outsym_size / 5));
        read_syms(insyms[i - 1], hdrs[i - 1].insym_size / 5, file);
        read_syms(outsyms[i - 1], hdrs[i - 1].outsym_size / 5, file);
        codes[i - 1] = (word_t*) malloc(sizeof(Sym) * hdrs[i - 1].code_size);
        read_code(codes[i - 1], hdrs[i - 1].code_size, file);

        fclose(file);

        // error checking
        if (check_mainx20(insyms[i - 1], hdrs[i - 1]))
            has_main = true;
    }

    if (!has_main)
    {
        printf("ERROR: Could not find main function\n");
        exit(1);
    }

    // get output file name
    int num_files;
    char out_name[20];
    if (strcmp(argv[argc - 2], "-o") == 0)
    {
        strcpy(out_name, argv[argc - 1]);
        num_files = argc - 3;
    }    
    else 
    {
        const char* dot = strchr(argv[1], '.');
        if (dot)
        {
            int len = dot - argv[1];
            strncpy(out_name, argv[1], len);
        }
        else
            strcpy(out_name, argv[1]);
        num_files = argc - 1;
    }
    strcat(out_name, ".exe");
    
    // get code into one array
    word_t* exe_code = get_code(num_files);

    int in_size = 0;
    int tot_out = 0;
    int tot_in = 0;
    // get all insyms into one array
    for (int i = 0; i < num_files; i++)
    {
        in_size += hdrs[i].insym_size;
        tot_out += hdrs[i].outsym_size / 5;
        tot_in += hdrs[i].insym_size / 5;
    }
        
    Sym* exe_insyms = (Sym*) malloc(sizeof(Sym) * in_size);
    adjust_insym_addr(num_files, exe_insyms);
    for (int i = 0; i < tot_in; i++)
    {
        for (int j = 0; j < tot_in; j++)
        {
            if (j != i && strcmp(exe_insyms[i].sym_name, exe_insyms[j].sym_name) == 0)
            {
                printf("ERROR: Duplicate insymbol definition %s\n", exe_insyms[i].sym_name);
                exit(0);
            }
        }
    }

    int ressed = res_syms(exe_code, exe_insyms, num_files, tot_in);
    if (tot_out > ressed)
    {
        printf("ERROR: Could not resolve all outsymbols\n");
        exit(1);
    }

    create_file(out_name, exe_code, exe_insyms, tot_in, num_files);
    free(exe_insyms);
}

Header read_header(FILE *file)
{
    Header hdr;
    if (fread(&hdr, sizeof(Header), 1, file) != 1)
    {
        printf("ERROR: Failed to read header info\n");
        exit(1);
    }

    return hdr;
}

void read_syms(Sym* syms, int num_syms, FILE* file)
{
    if (fread(syms, sizeof(Sym), num_syms, file) != num_syms)
    {
        printf("ERROR: Could not read in symbol info\n");
        exit(1);
    }
}

void read_code(word_t* code, int code_size, FILE* file)
{
    if (fread(code, 4, code_size, file) != code_size)
    {
        printf("ERROR: Could not read in code\n");
        exit(1);
    }
}

bool check_mainx20(Sym* syms, Header hdr)
{
    for (int i = 0; i < hdr.insym_size / 5; i++)
    {
        if (strcmp(syms[i].sym_name, "mainx20") == 0)
            return true;
    }

    return false;
}

int get_code_size(int num_files)
{
    int size = 0;
    for (int i = 0; i < num_files; i++)
        size += hdrs[i].code_size;

    return size;
}

word_t* get_code(int num_files)
{
    static word_t code[10000];
    int index = 0;

    for (int i = 0; i < num_files; i++)
    {
        for (int j = 0; j < hdrs[i].code_size; j++)
        {
            code[index] = codes[i][j];
            index++;
        }
    }

    return code;
}

void adjust_insym_addr(int num_files, Sym* exe_insyms)
{
    int index = 0;
    for (int i = 0; i < num_files; i++)
    {
        for (int j = 0; j < hdrs[i].insym_size / 5; j++)
        {
            exe_insyms[index] = insyms[i][j];
            int offset = get_code_size(i) + insyms[i][j].addr;
            if (i != 0)
                exe_insyms[index].addr += offset;
            index++;
        }
    }
}

int res_syms(word_t* code, Sym* exe_insyms, int num_files, int tot_in)
{
    int matches = 0;
    // first two loops check each file against each other
    for (int i = 0; i < num_files; i++)
    {
        for (int j = 0; j < num_files; j++)
        {
            if (i != j)
            {
                // iterate outsymbols
                for (int k = 0; k < hdrs[i].outsym_size / 5; k++)
                {
                    // iterate insymbols
                    for (int h = 0; h < hdrs[j].insym_size / 5; h++)
                    {
                        Sym out = outsyms[i][k];
                        Sym in = insyms[j][h];
                        // found a match
                        if (strcmp(out.sym_name, in.sym_name) == 0)
                        {
                            int index = get_code_size(i) + out.addr;
                            int args = get_args(code[index] & 0x000000FF);
                            Sym insym = get_exe_insym(in.sym_name, exe_insyms, tot_in);
                            if (insym.sym_name[0] == '\0')
                            {
                                printf("ERROR: Cannot find insym\n");
                                exit(1);
                            }
                            if (args == 0 || args == 2)
                            {
                                int addr = ((code[index] >> 12) | insym.addr);
                                addr -= (index + 1);
                                code[index] |= (addr << 12);
                            }
                            else if (args == 1)
                            {
                                int addr = ((code[index] >> 16) | insym.addr);
                                addr -= (index - 1);
                                code[index] |= (addr << 16);
                            }
                            else 
                            {
                                printf("ERROR: Code does not have an address");
                                exit(1);
                            }

                            matches++;
                        }
                    }
                }
            }
        }
    }
    return matches;
}

Sym get_exe_insym(char* insym_name, Sym* exe_insyms, int tot_in)
{
    for (int i = 0; i < tot_in; i++)
    {
        if (strcmp(exe_insyms[i].sym_name, insym_name) == 0)
            return exe_insyms[i];
    }
    Sym temp;
    temp.sym_name[0] = '\0';
    temp.addr = -1;
    return temp;
}

void create_file(char* file_name, word_t* code, Sym* insyms, int tot_in, int num_files)
{
    word_t* header = (word_t*) malloc(sizeof(word_t) * 3);
    header[0] = tot_in * 5;
    header[1] = 0;
    header[2] = get_code_size(num_files);

    FILE* file;
    file = fopen(file_name, "w");
    if (!file)
    {
        printf("ERROR: Could not make output file %s\n", file_name);
        exit(1);
    }

    // write header
    if (fwrite(header, sizeof(word_t), 3, file) != 3)
    {
        printf("ERROR: Could not write header to file\n");
        exit(1);
    }

    // write insymbols
    if (fwrite(insyms, sizeof(Sym), tot_in, file) != tot_in)
    {
        printf("ERROR: Could not write insymbols\n");
        exit(1);
    }

    // write code
    int code_size = get_code_size(num_files);
    if (fwrite(code, sizeof(word_t), code_size, file) != code_size)
    {
        printf("ERROR: Could not write code\n");
        exit(1);
    }

    fclose(file);
    free(header);
}

/*
0 (op addr)
1 (op reg reg addr)
2 (op reg addr)
*/
int get_args(int op)
{
    int res = 0;

    switch (op)
    {
        case 1:
            res = 2;
            break;
        case 2:
            res = 2;
            break;
        case 4:
            res = 2;
        case 15:
            res = 0;
            break;
        case 17:
            res = 1;
            break;
        case 18:
            res = 1;
            break;
        case 19:
            res = 1;
            break;
        case 21:
            res = 1;
            break;
        default:
            res = -1;
            break;
    }
    return res; 
}

void clean_up(int num_files)
{
    for (int i = 0; i < num_files; i++)
    {
        free(insyms[i]);
        free(outsyms[i]);
        free(codes[i]);
    }

    free(insyms);
    free(outsyms);
    free(hdrs);
    free(codes);
}