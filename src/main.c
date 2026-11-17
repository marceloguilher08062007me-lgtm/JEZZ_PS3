
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/process.h>
#include <sysutil/sysutil.h>
#include <audio/audio.h>

int main(void)
{
    printf("================================\n");
    printf("          JEZZ MUSIC\n");
    printf("================================\n");

    if (audioInit() != 0)
    {
        printf("Erro ao inicializar o audio.\n");
        return 1;
    }

    printf("Sistema de audio inicializado!\n");
    printf("JEZZ PS3 - Primeiro teste.\n");

    audioQuit();

    return 0;
}

