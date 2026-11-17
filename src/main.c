
#include <stdio.h>
#include <audio/audio.h>

int main(void)
{
    printf("JEZZ MUSIC\n");

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
