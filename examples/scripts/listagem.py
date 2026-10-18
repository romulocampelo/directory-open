"""Example for DirectoryOpen.ScriptInput v1; creates a new file only."""
import json
from pathlib import Path
import re
import sys

request = json.load(sys.stdin)
if request.get('contract') != 'DirectoryOpen.ScriptInput' or request.get('version') != 1:
    raise ValueError('Contrato de entrada não suportado.')
values = request['values']
name = values['name']
reserved = re.match(r'^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\.|$)', name, re.IGNORECASE)
if (not name or name in ('.', '..') or name.endswith((' ', '.'))
        or len(name.encode('utf-16-le')) > 510 or reserved
        or any(ord(c) < 32 or c in '<>:"/\\|?*' for c in name)):
    raise ValueError('Informe somente o nome de um arquivo novo, sem pastas.')
destination = Path(values['destination'])
if not destination.is_dir():
    raise ValueError('Escolha uma pasta de saída existente.')
paths = [Path(p) for p in request['selection']]
if values['order'] == 'Nome':
    paths.sort(key=lambda p: p.name.casefold())
lines = []
for path in paths:
    suffix = ''
    if values['sizes'] and path.is_file():
        suffix = f'\t{path.stat().st_size} bytes'
    lines.append(str(path) + suffix)
# The exclusive mode is intentional: never replace an existing output.
output = destination / name
with output.open('x', encoding='utf-8', newline='\n') as stream:
    stream.write('\n'.join(lines) + ('\n' if lines else ''))
print(f'Listagem criada em: {output}')
print(f'Locais relacionados: {len(paths)}. Confira o arquivo de saída.')
