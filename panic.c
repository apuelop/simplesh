#include <stdio.h>
#include <stdlib.h>

void panic(char *s)
{
    fprintf(stderr, "%s\n", s);
    exit(-1);
}
