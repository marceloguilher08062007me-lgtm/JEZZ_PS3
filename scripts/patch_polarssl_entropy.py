from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
source = path.read_text()

if "sysGetRandomNumber(output + produced" in source:
    print("PolarSSL já está corrigido.")
    raise SystemExit(0)

if "#include <lv2/system.h>" not in source:
    source = "#include <lv2/system.h>\n" + source

pattern = r'int platform_entropy_poll\s*\([^)]*\)\s*\{.*?\n\}'

replacement = r'''int platform_entropy_poll(void *data,
                           unsigned char *output, size_t len, size_t *olen)
{
    size_t produced = 0;
    (void) data;

    *olen = 0;

    while (produced < len)
    {
        size_t chunk = len - produced;

        if (chunk > RANDOM_NUMBER_MAX_SIZE)
            chunk = RANDOM_NUMBER_MAX_SIZE;

        if (sysGetRandomNumber(output + produced, chunk) != 0)
            return POLARSSL_ERR_ENTROPY_SOURCE_FAILED;

        produced += chunk;
    }

    *olen = produced;
    return 0;
}'''

source, count = re.subn(pattern, replacement, source, count=1, flags=re.S)

if count != 1:
    raise SystemExit("ERRO: função platform_entropy_poll não encontrada. Arquivo não foi salvo.")

path.write_text(source)
print("Correção de entropia PolarSSL preparada.")
