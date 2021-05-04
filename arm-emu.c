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

// ARM instruction enumeration
typedef enum {
    ARMBAD, // Bad instruction
    MOV,    // Move
    CMP,    // Compare
    BLT,    // Branch less then
} arm_opcode_t;

// Emulator instruction enumeration
typedef enum {
    EMUBAD, // Bad instruction
    LOAD,   // Load asm file
    STATE,  // State proc registers command
} emu_opcode_t;

// ARM instruction descriptor
static const char arminstr[][ARG_SIZE] = {
    [ARMBAD] = "",
    [MOV] = "mov",
    [CMP] = "cmp",
    [BLT] = "blt",
};

// Emulator instruction descriptor
static const char emuinstr[][ARG_SIZE] = {
    [EMUBAD] = "",
    [LOAD] = "load",
    [STATE] = "state",
};

// Emulator descriptor
typedef struct {
    cmd_t prog[MAX_INSTR];
} emu_t;

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
        // Remove commas
        if (cmd->raw_cmd[i] == ',') {
           cmd->raw_cmd[i] = ' ';
           continue;
        }

        // Remove newlines
        if (cmd->raw_cmd[i] == '\n') {
           cmd->raw_cmd[i] = '\0';
           continue;
        }

        // Remove tabs
        if (cmd->raw_cmd[i] == '\t') {
           cmd->raw_cmd[i] = ' ';
           continue;
        }
    }

    for (uint i = 0, cont = 1; cont && i < MAX_ARGS; ++i) {
        while (cmd->raw_cmd[pos1] == ' ') {
            ++pos1;
        }

        pos = strchr(cmd->raw_cmd + pos1, ' ');
        if (pos) {
            pos2 = pos - cmd->raw_cmd;
        } else {
            pos2 = pos1 + strlen(cmd->raw_cmd + pos1);
            cont = 0;
        }

        memcpy(cmd->args[i], cmd->raw_cmd + pos1, pos2 - pos1);
        
        pos1 = pos2;
    }

    return 0;
}

static void cmd_remlabel(cmd_t *cmd)
{
    for (uint i = 0; i < NELEMS(cmd->args) - 1; ++i) {
        strncpy(cmd->args[i], cmd->args[i + 1], strlen(cmd->args[i + 1]) + 1);
    }
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

static void cmd_printargs(cmd_t *cmd)
{
    printf("%s %s %s %s %s\n", cmd->args[0], cmd->args[1], cmd->args[2],
           cmd->args[3], cmd->args[4]);
}

static arm_opcode_t arm_getopcode(cmd_t *cmd)
{
    uint len = 0;

    for (uint i = 0; i < NELEMS(arminstr); ++i) {
        len = strlen(arminstr[i]);
        // printf("Instr: %s, len: %d\n", arminstr[i], len);
        if (len && strncmp(cmd->args[0], arminstr[i], len) == 0) {
            return i;
        }
    }

    return ARMBAD;
}

static emu_opcode_t emu_getopcode(cmd_t *cmd)
{
    uint len = 0;

    for (uint i = 0; i < NELEMS(emuinstr); ++i) {
        len = strlen(emuinstr[i]);
        if (len && strncmp(cmd->args[0], emuinstr[i], len) == 0) {
            return i;
        }
    }

    return EMUBAD;
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
        val = strtol(cmd->args[argnum] + 1, NULL, 0);
    }

    return val;
}

static void emu_printprog(emu_t *emu)
{
    printf("\033[33;1m");
    printf("PROG:\n");
    for (uint i = 0; i < NELEMS(emu->prog); ++i) {
        if (emu->prog[i].raw_cmd[0] == '\0') {
            break;
        }
        printf("\t");
        cmd_printraw(&emu->prog[i]);
        // cmd_printargs(&emu->prog[i]);
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

static void emu_resolvelabel(emu_t *emu, uint (*is_opcode)(cmd_t *))
{
    uint i = 0;
    uint argnum = 0;

    while (emu->prog[i].raw_cmd[0] != '\0') {
        if (!is_opcode(&emu->prog[i])) {
            // printf("Label: %s\n", emu->prog[i].args[0]);
            for (uint j = 0; j < NELEMS(emu->prog); ++j) {
                // Typically label is arg1 of each supported instruction, but if line contains
                // label it will be arg2
                argnum = is_opcode(&emu->prog[j]) ? 1 : 2;
                if (strncmp(emu->prog[i].args[0], emu->prog[j].args[argnum],
                    strlen(emu->prog[i].args[0])) == 0) {
                    // printf("Replacing: %s\n", emu->prog[j].args[argnum]);
                    snprintf(emu->prog[j].args[argnum], sizeof(emu->prog[j].args[argnum]), "#%d", i);
                    break;
                }
            }
            cmd_remlabel(&emu->prog[i]);
        }
        ++i;
    }
}

int arm_run(reg_t *regs, cmd_t *cmd)
{
    uint val1 = 0;
    uint val2 = 0;
    uint reg1 = 0;
    uint reg2 = 0;
    
    printf("\033[36m");
    printf("Run: PC - 0x%02X, instr - ", regs->pc);
    cmd_printargs(cmd);
    printf("\033[0m");

    switch (arm_getopcode(cmd)) {
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
            regs->pc = arm_getval(cmd, 1);
        } else {
            ++regs->pc;
        }
        break;
    default:
        printf("\033[31m");
        printf("Bad instruction! Trying to execute - %s\n", cmd->raw_cmd);
        printf("\033[0m");
        break;
    }

end:
    return regs->pc;
}

static uint emu_run(emu_t *emu, reg_t *regs, char *command)
{
    cmd_t cmd = {0};
    uint len = 0;
    FILE *instrf = NULL;

    len = strlen(command) > sizeof(cmd.raw_cmd) ?
        sizeof(cmd.raw_cmd) : strlen(command);

    memcpy(cmd.raw_cmd, command, len);
    cmd_parse(&cmd);

    switch (emu_getopcode(&cmd)) {
    case LOAD: {  
        // Load program from file
        uint cline = 0;
        instrf = fopen(cmd.args[1], "r");
        if (!instrf) {
            printf("File: %s - opening failed\n", cmd.args[1]);
            break;
        }

        while (!feof(instrf)) {
            memset(&cmd, 0, sizeof(cmd));
            fgets(cmd.raw_cmd, sizeof(cmd.raw_cmd), instrf);
            cmd_parse(&cmd);
            emu->prog[cline] = cmd;
            ++cline;
        }
        fclose(instrf);

        emu_printprog(emu);

        // Resolve labels
        emu_resolvelabel(emu, arm_getopcode);

        // Run program
        memset(regs, 0, sizeof(*regs));
        uint ppc = regs->pc;
        uint cpc = regs->pc;
        while (emu->prog[regs->pc].raw_cmd[0] != '\0') {
            arm_run(regs, &(emu->prog[regs->pc]));
            cpc = regs->pc;
            if (cpc == ppc) {
                printf("\033[31;1m");
                printf("Executing loop instruction. Halting\n");
                printf("\033[0m");
                break;
            }
            ppc = cpc;
        }

        break;
    }
    case STATE:
        arm_state(regs);
        break;
    default:
        arm_run(regs, &cmd);
        break;
    }

    return 0;
}

int main(void)
{
    emu_t emu = {0};
    reg_t registers = {0};
    char command[COMMAND_SIZE] = {0};

    while (true) {
        printf("%s> ", PROMPT);
        fgets(command, sizeof(command), stdin);
        emu_run(&emu, &registers, command);
    }

    return 0;
}
