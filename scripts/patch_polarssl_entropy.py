from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
source = path.read_text()

if "#include <lv2/system.h>" not in source:
    source = "#include <lv2/system.h>\n" + source

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

pattern = r'int platform_entropy_poll\s*\([^)]*\)\s*\{.*?^\}'
source, count = re.subn(
    pattern, replacement, source,
    flags=re.S | re.M
)

if count < 1:
    raise SystemExit("ERRO: nenhuma implementação encontrada.")

if "fopen(" in source or "fread(" in source:
    raise SystemExit("ERRO: ainda existem chamadas antigas no arquivo.")

if "sysGetRandomNumber(output + produced" not in source:
    raise SystemExit("ERRO: chamada ao gerador não encontrada.")

path.write_text(source)
print(f"PolarSSL corrigido: {count} implementação(ões) substituída(s).")
