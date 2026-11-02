from pathlib import Path
import hashlib
from elftools.elf.elffile import ELFFile
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
import argparse
parser=argparse.ArgumentParser(description='Patch the exact amaral.3 diagnostic ELF for the one-worker experiment')
parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
args=parser.parse_args();src=args.source;data=bytearray(src.read_bytes())
assert hashlib.sha256(data).hexdigest()=='7ab5ceec7079290952d3004f2bff27b22b84bc69e1f6f2fbace39638407e9bea', 'Unexpected input ELF: refusing patch' 
with src.open('rb') as f:
 e=ELFFile(f);sym=next(s for s in e.get_section_by_name('.symtab').iter_symbols() if s.name=='run_job');sec=e.get_section(sym['st_shndx']);off=sec['sh_offset']+sym['st_value']-sec['sh_addr']
ins=list(Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(data[off:off+sym['st_size']],sym['st_value']))
target=[i for i in ins if i.mnemonic=='csel' and i.op_str=='w22, w8, w9, lt'];assert len(target)==1
i=target[0];where=off+i.address-sym['st_value'];assert data[where:where+4]==bytes.fromhex('16b1891a')
data[where:where+4]=bytes.fromhex('36008052') # mov w22,#1: final worker count, also overrides env
assert list(Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(data[where:where+4],i.address))[0].op_str=='w22, #1'
a=b'amaral.3 diagnostic build:';b=b'amaral.4 diagnostic build:';assert data.count(a)==1;data=data.replace(a,b)
args.output.write_bytes(data)
print('Original ELF SHA256',hashlib.sha256(src.read_bytes()).hexdigest())
print('Changed final worker count instruction at',hex(i.address),'to mov w22,#1; diagnostic banner updated')
