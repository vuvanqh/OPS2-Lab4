#include "l8_common.h"

#define BACKLOG 3
#define MAX_EVENTS 16

volatile sig_atomic_t do_work = 1;

void sigint_handler(int sig) { do_work = 0; }

void usage(char *name) { fprintf(stderr, "USAGE: %s socket port\n", name); }


void calculate(int32_t data[5])
{
    int32_t op1, op2, result = -1, status = 1;
    op1 = ntohl(data[0]);
    op2 = ntohl(data[1]);
    switch ((char)ntohl(data[3]))
    {
        case '+':
            result = op1 + op2;
            break;
        case '-':
            result = op1 - op2;
            break;
        case '*':
            result = op1 * op2;
            break;
        case '/':
            if (!op2)
                status = 0;
            else
                result = op1 / op2;
            break;
        default:
            status = 0;
    }
    data[4] = htonl(status);
    data[2] = htonl(result);
}
