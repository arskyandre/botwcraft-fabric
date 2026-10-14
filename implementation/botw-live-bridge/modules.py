"""Read-only loaded module audit, verified PID/creation/executable identity."""
import ctypes as C,ctypes.wintypes as W,json,pathlib,hashlib,struct
import session
class Entry(C.Structure):
    _fields_=[('size',W.DWORD),('module_id',W.DWORD),('pid',W.DWORD),('global_use',W.DWORD),('process_use',W.DWORD),('base',C.c_void_p),('bytes',W.DWORD),('module',W.HMODULE),('name',W.WCHAR*256),('path',W.WCHAR*260)]
def audit(lab=session.LAB):
    data=json.loads((lab/'process.json').read_text());h=session.openp(0x100410,False,data['pid']);assert h
    snapshot=session.api('CreateToolhelp32Snapshot',W.HANDLE,[W.DWORD,W.DWORD]);first=session.api('Module32FirstW',W.BOOL,[W.HANDLE,C.POINTER(Entry)]);next_entry=session.api('Module32NextW',W.BOOL,[W.HANDLE,C.POINTER(Entry)])
    try:
        session.verify(h,data);s=snapshot(0x18,data['pid']);assert s and s!=C.c_void_p(-1).value
        try:
            entry=Entry();entry.size=C.sizeof(entry);rows=[];ok=first(s,C.byref(entry))
            while ok:rows.append({'name':entry.name,'path':entry.path,'base':entry.base,'bytes':entry.bytes});ok=next_entry(s,C.byref(entry))
        finally:session.close(s)
        probes=[r for r in rows if r['name'].lower() in ('botw_live_bridge.dll','botw_bridge.dll','body_probe.dll','render_probe.dll')]
        assert len(probes)==int(data['probe_injected']),'Unexpected native adapter module count'
        if probes:
            assert probes[0]['name'].lower()=='botw_live_bridge.dll','Foreign B/C DLL in E session'
            assert pathlib.Path(probes[0]['path']).resolve()==lab/'botw_live_bridge.dll','Adapter loaded outside E lab'
            # Known owned PE section only, in bounded read chunks outside all
            # game callbacks. Store digests, never extracted executable bytes.
            probe=probes[0];raw=pathlib.Path(probe['path']).read_bytes()
            pe=struct.unpack_from('<I',raw,0x3c)[0];assert raw[:2]==b'MZ' and raw[pe:pe+4]==b'PE\0\0'
            count=struct.unpack_from('<H',raw,pe+6)[0];optional=struct.unpack_from('<H',raw,pe+20)[0];assert 0<count<=96
            table=pe+24+optional;text=[]
            for index in range(count):
                section=table+index*40
                if raw[section:section+8].rstrip(b'\0')!=b'.text':continue
                size,rva,raw_size,offset=struct.unpack_from('<IIII',raw,section+8)
                assert 0<size<=4*1024*1024 and rva+size<=probe['bytes'] and offset+raw_size<=len(raw)
                read=session.api('ReadProcessMemory',W.BOOL,[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)])
                digest=hashlib.sha256()
                for position in range(0,size,65536):
                    length=min(65536,size-position);buffer=C.create_string_buffer(length);copied=C.c_size_t()
                    assert read(h,probe['base']+rva+position,buffer,length,C.byref(copied)) and copied.value==length
                    digest.update(buffer.raw)
                disk=raw[offset:offset+min(size,raw_size)]+bytes(max(0,size-raw_size))
                text.append({'rva':rva,'virtual_size':size,'loaded_memory_sha256':digest.hexdigest(),'disk_virtual_section_sha256':hashlib.sha256(disk).hexdigest()})
            assert len(text)==1;probe['file_sha256']=hashlib.sha256(raw).hexdigest();probe['text_section']=text[0]
            probe['hash_scope']='On-disk complete DLL and live known .text section; no relocation normalization or mutable-data image hash.'
        session.verify(h,data)
        return {'identity':data,'modules':rows,'native_adapter_modules':probes}
    finally:session.close(h)
