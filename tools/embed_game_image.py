#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Convert a user-owned unpacked TESV to link-time data and resolved-import metadata.
No runtime PE parser or external game executable is needed in the resulting build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def parse_image(data):
    def check(offset, size, limit=None):
        limit=len(data) if limit is None else limit
        if offset<0 or size<0 or offset>limit or size>limit-offset:
            raise ValueError('truncated or out-of-range image data')
    def u16(at):
        check(at,2);return struct.unpack_from('<H',data,at)[0]
    def u32(at):
        check(at,4);return struct.unpack_from('<I',data,at)[0]
    if data[:2]!=b'MZ':raise ValueError('not an MZ executable')
    pe=u32(60);check(pe,24)
    if data[pe:pe+4]!=b'PE\0\0' or u16(pe+4)!=0x14c:raise ValueError('requires x86 PE32 TESV')
    count=u16(pe+6);opt=pe+24;optsize=u16(pe+20)
    check(opt,optsize)
    if optsize<224 or u16(opt)!=0x10b or not 1<=count<=96:raise ValueError('invalid PE32 headers')
    base=u32(opt+28);entry=u32(opt+16);size=u32(opt+56);headers=u32(opt+60)
    if base!=0x400000 or not 0<entry<size<=0x10000000:raise ValueError('unsupported Skyrim image layout')
    check(0,headers);check(headers,0,size)
    if headers<opt+optsize+count*40:raise ValueError('section table outside headers')
    image=bytearray(size);segments=[(base,data[:headers])];image[:headers]=data[:headers]
    spans=[(0,headers)]
    for n in range(count):
        at=opt+optsize+n*40;check(at,40)
        name=data[at:at+8].split(b'\0')[0]
        if name==b'.bind':raise ValueError('SteamStub image: supply an unpacked TESV')
        virtual=u32(at+8);rva=u32(at+12);rawsize=u32(at+16);raw=u32(at+20)
        memory_size=virtual or rawsize;copy=min(rawsize,memory_size)
        check(rva,memory_size,size);check(raw,rawsize)
        if memory_size and any(rva<end and start<rva+memory_size for start,end in spans):raise ValueError('overlapping sections')
        spans.append((rva,rva+memory_size))
        if copy:
            image[rva:rva+copy]=data[raw:raw+copy];segments.append((base+rva,data[raw:raw+copy]))
    def mapped32(at):
        check(at,4,size);return struct.unpack_from('<I',image,at)[0]
    def string(at):
        check(at,1,size);end=image.find(0,at,min(at+4096,size))
        if end<0:raise ValueError('unterminated import name')
        return bytes(image[at:end]).decode('ascii')
    if u32(opt+96):raise ValueError('TESV exports are not supported')
    imports=[];rva=u32(opt+104);directory_size=u32(opt+108)
    if rva:
        check(rva,directory_size,size)
        for d in range(rva,rva+directory_size,20):
            check(d,20,size)
            if not mapped32(d+12):break
            dll=string(mapped32(d+12));ilt=mapped32(d) or mapped32(d+16);iat=mapped32(d+16)
            for index in range(65536):
                value=mapped32(ilt+index*4)
                if not value:break
                check(iat+index*4,4,size)
                name=f'#{value&65535}' if value&0x80000000 else string(value+2)
                imports.append((base+iat+index*4,dll,name))
            else:raise ValueError('unterminated imports')
        else:raise ValueError('unterminated import descriptors')
    tls=[0]*5;tls_rva=u32(opt+168)
    if tls_rva:
        check(tls_rva,24,size)
        start,end,index,callbacks,zero=struct.unpack_from('<5I',image,tls_rva)
        if start or end:
            if end<start:raise ValueError('invalid TLS range')
            check(start-base,end-start,size)
        if index:check(index-base,4,size)
        if callbacks:check(callbacks-base,4,size)
        tls=[start,end,index,zero,callbacks]
    return dict(base=base,size=size,entry=base+entry,tls=tls,segments=segments,imports=imports,sha256=hashlib.sha256(data).hexdigest())


def generate(source, output):
    parsed=parse_image(Path(source).read_bytes());output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True)
    blob=bytearray();rows=[]
    for address,chunk in parsed['segments']:
        rows.append(f'    {{0x{address:x}u,{len(blob)}u,{len(chunk)}u}},');blob+=chunk
    (output/'skyrim_image.bin').write_bytes(blob)
    lines=['/* Generated from a private game input. Do not publish. */','#include "embedded_image.h"','static const SkydreamImageSegment segments[] = {',*rows,'};','static const SkydreamImageImport imports[] = {']
    for address,dll,name in parsed['imports']:lines.append(f'    {{0x{address:x}u,{json.dumps(dll)},{json.dumps(name)}}},')
    if not parsed['imports']:lines.append('    {0,0,0},')
    lines+=['};','const SkydreamImage g_skyrim_image = {',','.join(str(parsed[key])+'u' for key in ('base','size','entry'))+',',','.join(str(x)+'u' for x in parsed['tls'])+',',json.dumps(parsed['sha256'])+',',f'{len(rows)}u,{len(parsed["imports"])}u,segments,imports','};']
    (output/'skyrim_image.c').write_text('\n'.join(lines)+'\n')
    path=str(output/'skyrim_image.bin')
    if any(c in path for c in '\n\r'):raise ValueError('invalid output path')
    path=path.replace('\\','\\\\').replace('"','\\"')
    (output/'skyrim_image.S').write_text('.section .rodata\n.balign 16\n.global g_skyrim_image_blob\ng_skyrim_image_blob:\n.incbin "'+path+'"\n.section .note.GNU-stack,""\n')
    summary={key:parsed[key] for key in ('base','size','entry','tls','sha256')}
    summary.update(segment_count=len(rows),import_count=len(parsed['imports']),blob_bytes=len(blob))
    (output/'skyrim_image.json').write_text(json.dumps(summary,indent=2)+'\n')
    return summary

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
    try:print(json.dumps(generate(args.source,args.output),indent=2))
    except (ValueError,UnicodeError) as e:parser.exit(1,str(e)+'\n')
