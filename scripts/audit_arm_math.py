#!/usr/bin/env python3
"""Audit arithmetic instructions and helper calls in ARM ELF hot functions."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

DEFAULT = ('dtr_arc_f','dtr_arc_bands','dtr_line','dtr_spindle','dtr_text','render')
FORBIDDEN = re.compile(r'^(?:__aeabi_d|__aeabi_[ul]?ldivmod|__udivmoddi4|(?:sin|cos|tan|atan2?|acos|pow|fmod)(?:f)?)$')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf',type=Path)
    parser.add_argument('--function',action='append',dest='functions')
    parser.add_argument('--objdump',default='arm-zephyr-eabi-objdump')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    tool=shutil.which(args.objdump) or (args.objdump if Path(args.objdump).is_file() else None)
    if not tool:parser.error(f'objdump not found: {args.objdump}')
    text=subprocess.check_output([tool,'-d',str(args.elf)],text=True)
    bodies={};current=None
    for line in text.splitlines():
        match=re.match(r'^[0-9a-f]+ <([^>]+)>:$',line)
        if match:current=match.group(1);bodies.setdefault(current,[])
        elif current:bodies[current].append(line)
    report={'elf':str(args.elf),'functions':{},'forbidden_calls':[]}
    for name in args.functions or DEFAULT:
        if name not in bodies:raise SystemExit(f'missing ELF function: {name}')
        body='\n'.join(bodies[name]);calls=re.findall(r'\bbl(?:x)?\s+[^<]*<([^>+]+)',body)
        forbidden=sorted({target for target in calls if FORBIDDEN.match(target)})
        report['functions'][name]={
            'vdiv_f32':len(re.findall(r'\bvdiv\.f32\b',body)),
            'vsqrt_f32':len(re.findall(r'\bvsqrt\.f32\b',body)),
            'sdiv':len(re.findall(r'\bsdiv\b',body)),
            'sqrtf_calls':sum(target=='sqrtf' for target in calls),
            'forbidden_calls':forbidden,
        }
        report['forbidden_calls'] += [f'{name}->{target}' for target in forbidden]
    rendered=json.dumps(report,indent=2)+'\n'
    if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(rendered)
    print(rendered,end='')
    if report['forbidden_calls']:raise SystemExit(1)

if __name__=='__main__':main()
