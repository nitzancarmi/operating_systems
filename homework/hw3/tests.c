#include<stdio.h>

int main () {

    int i;
    for (i=5; (i--)>0; printf("%d\n", i));
    return 0;
}