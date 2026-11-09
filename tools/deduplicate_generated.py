#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit exact lifted-function duplicates. Only local label names are normalized.
Numeric constants, guest addresses, calls, strings and ABI operations are preserved.
"""
import argparse,hashlib,json,re
from collections import defaultdict
from pathlib import Path
FUNCTION=re.compile(r'^void f_([0-9A-F]+)(_guest)?\(CPU \*c\) \{\n.*?^\}\n',re.M|re.S)
LABEL=re.compile(r'\bL_[0-9A-F]+\b')
C_TOKEN=re.compile(r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|/\*.*?\*/|//[^\n]*|\bL_[0-9A-F]+\b''',re.S)
MARK=re.compile(r'/\* SKYDREAM_DEDUP: (f_[0-9A-F]+) \*/')

def normalize(text):
    body=text[text.index('\n')+1:];labels={}
    definitions=set()
    for token in C_TOKEN.finditer(body):
        if LABEL.fullmatch(token.group()) and body[token.end():token.end()+1]==':' and (token.start()==0 or body[token.start()-1]=='\n'):
            definitions.add(token.group())
    def label(match):
        name=match.group()
        if name not in definitions:return name
        definition=body[match.end():match.end()+1]==':' and (match.start()==0 or body[match.start()-1]=='\n')
        jump=re.search(r'\bgoto\s+$',body[max(0,match.start()-32):match.start()])
        if not definition and not jump:return name
        return labels.setdefault(name,'L_local_'+str(len(labels)))
    return C_TOKEN.sub(label,body)

def family(body):
    if re.search(r'\bf_[0-9A-F]+\(c\)|\brt_call',body):return 'call/thunk'
    if re.search(r'TS\d|M(?:8|16|32|64)\([^\n]*=',body):return 'memory-write-pattern'
    if re.search(r'TL\d|M(?:8|16|32|64)\(',body):return 'memory-read-pattern'
    if 'fpush' in body or 'ST(' in body or 'c->xmm' in body:return 'numeric/SIMD'
    return 'register/return'

def audit(folder,apply=False):
    folder=Path(folder);buckets=defaultdict(list);files={};existing={};excluded=0;total=0
    for path in sorted(folder.glob('exe_*.c')):
        text=path.read_text();files[path]=text
        for match in FUNCTION.finditer(text):
            total+=1;name='f_'+match[1];body=match.group();mark=MARK.search(body)
            if mark:existing[name]=mark[1];continue
            if int(match[1],16)==0x8F9720 or match[2] or re.search(r'\bstatic\b|setjmp|__builtin|return_address',body):excluded+=1;continue
            normalized=normalize(body);key=hashlib.sha256(normalized.encode()).hexdigest()
            buckets[key].append((int(match[1],16),name,path,match.start(),match.end(),normalized))
    shapes=defaultdict(list)
    for items in buckets.values():
        for item in items:
            shape=re.sub(r'0x[0-9A-Fa-f]+u?|\bf_[0-9A-F]+\b','VALUE',item[5])
            shapes[hashlib.sha256(shape.encode()).hexdigest()].append(item[1])
    candidate_families=[{'members':names,'count':len(names)} for names in shapes.values() if len(names)>1]
    groups=[];replacements=defaultdict(list);removed=0;aliases=dict(existing);by_family=defaultdict(lambda:{'groups':0,'duplicates':0})
    for key,items in buckets.items():
        if len(items)<2:continue
        items.sort();canonical=items[0]
        assert all(item[5]==canonical[5] for item in items) # verify equality beyond hash
        kind=family(canonical[5]);by_family[kind]['groups']+=1;by_family[kind]['duplicates']+=len(items)-1
        marker='/* SKYDREAM_DEDUP_CANONICAL */\nvoid '+canonical[1]+'(CPU *c) __attribute__((noinline));\n'
        # A prototype keeps the normal definition syntax intact for all tools.
        if marker not in files[canonical[2]]:
            replacements[canonical[2]].append((canonical[3],canonical[3],marker))
            removed-=len(marker)

        groups.append({'canonical':canonical[1],'members':[x[1] for x in items],'family':kind,'normalized_sha256':key})
        for item in items[1:]:
            aliases[item[1]]=canonical[1]
            wrapper='void '+item[1]+'(CPU *c) {\n    /* SKYDREAM_DEDUP: '+canonical[1]+' */\n    '+canonical[1]+'(c);\n}\n'
            replacements[item[2]].append((item[3],item[4],wrapper));removed+=item[4]-item[3]-len(wrapper)
    if apply:
        for path,changes in replacements.items():
            text=files[path]
            for start,end,replacement in sorted(changes,reverse=True):text=text[:start]+replacement+text[end:]
            path.write_text(text)
    report={'total_entry_points':total,'excluded_host_or_stateful_bodies':excluded,'new_duplicate_groups':len(groups),'new_duplicate_bodies':sum(len(x['members'])-1 for x in groups),'existing_aliases':len(existing),'estimated_source_bytes_removed':removed,'families':dict(by_family),'groups':sorted(groups,key=lambda x:-len(x['members'])),'aliases':aliases,'review_only_structural_families':sorted(candidate_families,key=lambda x:-x['count']),'applied':apply}
    return report

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('folder',type=Path);ap.add_argument('--apply',action='store_true');ap.add_argument('--report',type=Path);a=ap.parse_args();r=audit(a.folder,a.apply)
    if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k not in ('groups','aliases','review_only_structural_families')},indent=2))
    for group in r['groups'][:10]:print(group['canonical'],len(group['members']),group['family'])
