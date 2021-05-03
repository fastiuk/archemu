#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// Utils
#define NELEMS(arr)             (arr ? (sizeof(arr) / sizeof(arr[0])) : 0)
#define GETBIT(val, bit)        ((val & (1 << bit)) >> bit)
#define SETBIT(val, bit, state) (state ? (val |= (1 << bit)) : (val &= ~(1 << bit)))

// Comands params
#define COMMAND_SIZE    128
#define ARG_SIZE        64
#define MAX_ARGS        5
#define MAX_INSTR       128

// Emulator params
#define PROMPT  "arm-emu"

typedef unsigned int uint;

// Command descriptor
typedef struct {
    char raw_cmd[COMMAND_SIZE];
    char args[MAX_ARGS][ARG_SIZE];  // Command parsed into the next format:
                                    // instruction arg1 arg2 arg3 arg4
    char is_label;                  // Indicates if parsed line is a label    
} cmd_t;

// CortexM registers
typedef struct {
    uint r[13]; // R0-R12
    uint sp;    // Stack Pointer (R13)
    uint lr;    // Link Register (R14)
    uint pc;    // Program Counter (R15)
    uint psr;   // Program Status Register
} reg_t;

// Program Status Register flags
enum {
    PSR_Q = 27,
    PSR_V,
    PSR_C,
    PSR_Z,
    PSR_N,
};

// Emulator instruction enumeration
typedef enum {
    BAD,    // Bad instruction
    STATE,  // State command - emulator instruction
    LOAD,   // Load asm file - emulator instruction
    MOV,    // Move
    CMP,    // Compare
    BLT,    // Branch less then
} opcode_t;

// Emulator instruction descriptor
static const char arminstr[][ARG_SIZE] = {
    [BAD] = "",
    [LOAD] = "load",
    [STATE] = "state",
    [MOV] = "mov",
    [CMP] = "cmp",
    [BLT] = "blt",
};

// Label descriptor
typedef struct {
    char label[ARG_SIZE];
    uint pc;
} asml_t;

// Emulator descriptor
typedef struct {
    cmd_t prog[MAX_INSTR];
    asml_t ld[MAX_INSTR];
} emu_t;

emu_t emu = {0};

static int cmd_parse(cmd_t *cmd)
{
    char *pos = NULL;
    uint pos1 = 0;
    uint pos2 = 0;

    pos = cmd->raw_cmd;

    // Convert all to lowercase
    for (uint i = 0; i <= strlen(cmd->raw_cmd); ++i) {
        cmd->raw_cmd[i] = tolower(cmd->raw_cmd[i]);
    }

    // Remove all special symbols
    for (uint i = 0; i <= strlen(cmd->raw_cmd); ++i) {
        // Remove comma
        if (cmd->raw_cmd[i] == ',') {
           cmd->raw_cmd[i] = ' ';
           continue;
        }

        // Remove newlines
        if (cmd->raw_cmd[i] == '\n') {
           cmd->raw_cmd[i] = '\0';
           continue;
        }
    }

    for (uint i = 0, cont = 1; cont && i < MAX_ARGS; ++i) {
        pos = strchr(cmd->raw_cmd + pos1, ' ');
        if (pos) {
            pos2 = pos - cmd->raw_cmd;
        } else {
            pos2 = pos1 + strlen(cmd->raw_cmd + pos1);
            cont = 0;
        }

        memcpy(cmd->args[i], cmd->raw_cmd + pos1, pos2 - pos1);
        
        pos1 = pos2;
        while (cmd->raw_cmd[pos1] == ' ') {
            ++pos1;
        }
    }

    // Check for a label
    if (cmd->args[0][strlen(cmd->args[0]) - 1] == ':') {
        cmd->is_label = 1;
    }

    return 0;
}

static void cmd_print(cmd_t *cmd)
{
    if (cmd->raw_cmd[0] != '\0') {
        printf("Raw command:\n\t%s\n", cmd->raw_cmd);
    }

    if (cmd->args[0][0] != '\0') {
        printf("Args:\n");

        for (uint i = 0; i < MAX_ARGS; ++i) {
            if (cmd->args[i][0] == '\0') {
                break;
            }
            printf("\t%s\n", cmd->args[i]);
        }
    }
}

static void cmd_printraw(cmd_t *cmd)
{
    printf("%s\n", cmd->raw_cmd);
}

static opcode_t arm_getopcode(cmd_t *cmd)
{
    uint len = 0;

    for (uint i = 0; i < NELEMS(arminstr); ++i) {
        len = strlen(arminstr[i]);
        // printf("Instr: %s, len: %d\n", arminstr[i], len);
        if (len && strncmp(cmd->args[0], arminstr[i], len) == 0) {
            return i;
        }
    }

    return 0;
}

static uint arm_getregnum(cmd_t *cmd, uint argnum)
{
    uint num = 0;

    if ((argnum < 1) || (argnum > 4)) {
        return -1;
    }

    if (cmd->args[argnum][0] == 'r') {
        num = strtol(cmd->args[argnum] + 1, NULL, 10);
        if ((num >= 0) && (num <= 12)) {
            return num;
        }
    }

    return -1;
}

static uint arm_getval(cmd_t *cmd, uint argnum)
{
    uint val = -1;

    if (cmd->args[argnum][0] == '#') {
        if ((cmd->args[argnum][1] == '0') && (cmd->args[argnum][2] == 'x')) {
            val = strtol(cmd->args[argnum] + 3, NULL, 16);
        }
        val = strtol(cmd->args[argnum] + 1, NULL, 10);
    }

    return val;
}

static uint emu_getlabelpc(emu_t *emu, char *label)
{
    for (uint i = 0; i < NELEMS(emu->ld); ++i) {
        if (strncmp(emu->ld[i].label, label, strlen(label)) == 0) {
            return emu->ld[i].pc;
        }
    }

    return -1;
}

static void emu_printprog(emu_t *emu)
{
    printf("\033[34;1m");
    printf("PROG:\n");
    for (uint i = 0; i < NELEMS(emu->prog); ++i) {
        if (emu->prog[i].raw_cmd[0] == '\0') {
            break;
        }
        printf("\t");
        cmd_printraw(&emu->prog[i]);
    }
    printf("\033[0m");
}

static void emu_printlabels(emu_t *emu)
{
    printf("\033[33;1m");
    printf("LABELS:\n");
    for (uint i = 0; i < NELEMS(emu->ld); ++i) {
        if (emu->ld[i].label[0] == '\0') {
            break;
        }
        printf("\tLabel: %s, PC: %d\n", emu->ld[i].label, emu->ld[i].pc);
    }
    printf("\033[0m");
}

static void arm_state(reg_t *regs)
{
    printf("\033[32;1m");
    printf("ARM processor emulator state:\n");
    printf("\tR0:\t0x%08X\n", regs->r[0]);
    printf("\tR1:\t0x%08X\n", regs->r[1]);
    printf("\tR2:\t0x%08X\n", regs->r[2]);
    printf("\tR3:\t0x%08X\n", regs->r[3]);
    printf("\tR4:\t0x%08X\n", regs->r[4]);
    printf("\tR5:\t0x%08X\n", regs->r[5]);
    printf("\tR6:\t0x%08X\n", regs->r[6]);
    printf("\tR7:\t0x%08X\n", regs->r[7]);
    printf("\tR8:\t0x%08X\n", regs->r[8]);
    printf("\tR9:\t0x%08X\n", regs->r[9]);
    printf("\tR10:\t0x%08X\n", regs->r[10]);
    printf("\tR11:\t0x%08X\n", regs->r[11]);
    printf("\tR12:\t0x%08X\n", regs->r[12]);
    printf("\tSP:\t0x%08X\n", regs->sp);
    printf("\tLR:\t0x%08X\n", regs->lr);
    printf("\tPC:\t0x%08X\n", regs->pc);
    printf("\tPSR: N: %d, Z: %d, C: %d, V: %d, Q: %d\n",
           GETBIT(regs->psr, PSR_N),
           GETBIT(regs->psr, PSR_Z),
           GETBIT(regs->psr, PSR_C),
           GETBIT(regs->psr, PSR_V),
           GETBIT(regs->psr, PSR_Q));
    printf("\033[0m");
}

static void emu_addlabel(emu_t *emu, cmd_t *cmd, uint pc)
{
    for (uint i = 0; i < NELEMS(emu->ld); ++i) {
        if (emu->ld[i].label[0] == '\0') {
            memcpy(emu->ld[i].label, cmd->args[0], strlen(cmd->args[0]));
            emu->ld[i].pc = pc;
            break;
        }
    }
}

int arm_run(reg_t *regs, cmd_t *cmd)
{
    opcode_t op = 0;
    uint val1 = 0;
    uint val2 = 0;
    uint reg1 = 0;
    uint reg2 = 0;
    FILE *instrf = NULL;
    cmd_t cmd1 = {0};

    cmd_parse(cmd);
    
    printf("Run: PC - %08X, instr - ", regs->pc);
    cmd_printraw(cmd);

    op = arm_getopcode(cmd);
    // printf("Opcode: %d\n", op);

    switch (op) {
    case LOAD: {// TODO: wip        
        // Load program from file
        uint cline = 0;
        instrf = fopen(cmd->args[1], "r");
        if (!instrf) {
            printf("File: %s - opening failed\n", cmd->args[1]);
            break;
        }

        while (!feof(instrf)) {
            memset(&cmd1, 0, sizeof(cmd1));
            fgets(cmd1.raw_cmd, sizeof(cmd1.raw_cmd), instrf);
            cmd_parse(&cmd1);
            // printf("Read line: %s\n", cmd1.raw_cmd);
            emu.prog[cline] = cmd1;
            ++cline;
        }
        fclose(instrf);

        // Add labels
        uint i = 0;
        while (emu.prog[i].raw_cmd[0] != '\0') {
            if (emu.prog[i].is_label) {
                emu_addlabel(&emu, &emu.prog[i], i);
            }
            ++i;
        }

        emu_printprog(&emu);
        emu_printlabels(&emu);

        // Run program
        memset(regs, 0, sizeof(*regs));
        i = 0;
        while (emu.prog[regs->pc].raw_cmd[0] != '\0') {
            // printf("PC: %d\n", regs->pc);
            arm_run(regs, &(emu.prog[regs->pc]));
            ++i;
            if (i > 20) {
                break;
            }
        }

        break;
    }
    case STATE:
        arm_state(regs);
        break;
    case MOV:
        reg1 = arm_getregnum(cmd, 1);
        reg2 = arm_getregnum(cmd, 2);
        if (reg2 != -1) {
            regs->r[reg1] = regs->r[reg2];
        } else {
            val1 = arm_getval(cmd, 2);
            regs->r[reg1] = val1;
        }
        ++regs->pc;
        break;
    case CMP:
        reg1 = arm_getregnum(cmd, 1);
        val1 = regs->r[reg1];

        reg2 = arm_getregnum(cmd, 2);
        if (reg2 != -1) {
            val2 = regs->r[reg2];
        } else {
            val2 = arm_getval(cmd, 2);
        }

        if (val1 == val2) {
            SETBIT(regs->psr, PSR_Z, 1);
            SETBIT(regs->psr, PSR_N, 0);
        } else if (val1 > val2) {
            SETBIT(regs->psr, PSR_Z, 0);
            SETBIT(regs->psr, PSR_N, 0);
        } else {
            SETBIT(regs->psr, PSR_Z, 0);
            SETBIT(regs->psr, PSR_N, 1);   
        }

        SETBIT(regs->psr, PSR_C, 0);
        ++regs->pc;
        break;
    case BLT:
        if (!GETBIT(regs->psr, PSR_Z) && GETBIT(regs->psr, PSR_N)) {
            regs->pc = emu_getlabelpc(&emu, cmd->args[1]);
        } else {
            ++regs->pc;
        }
        break;
    default:
        if (cmd->is_label) {
            ++regs->pc;
            break;
        }
        printf("Bad instruction! Trying to execute - %s\n", cmd->raw_cmd);
        break;
    }

end:
    return regs->pc;
}

int main(void)
{
    cmd_t command = {0};
    reg_t registers = {0};
    // emu_t emu = {0};

    while (true) {
        memset(&command, 0, sizeof(command));
        printf("%s> ", PROMPT);
        fgets(command.raw_cmd, sizeof(command.raw_cmd), stdin);
        arm_run(&registers, &command);
    }

    return 0;
}
