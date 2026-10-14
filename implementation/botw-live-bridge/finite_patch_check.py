"""Offline synthetic parser/raster fixture; never native geometry evidence."""
import pathlib,struct
import finite_patch as p

def main():
    source={'support_tolerance':.03,'scale':1.3,'origin_mc':[0,100,0],'anchor':[0,0,0],
        'bounds_xz_relative':[-2,2,-2,2],'feet_y_relative':[-1,6],
        'triangles':[{'vertices':[[-2,0,-2],[-2,0,2],[0,1,2]],'error':0,'flags':0},
                     {'vertices':[[-2,0,-2],[0,1,2],[0,1,-2]],'error':0,'flags':0}]}
    data=p.pack(source);assert data[:4]==b'FPC2';count=struct.unpack_from('<I',data,4)[0]
    offset=84;regions=[];known_empty=0
    for i in range(count):
        typ,n=struct.unpack_from('<II',data,offset);offset+=8
        assert typ==(1 if i==0 else 3 if i%2 else 2)
        if typ!=1:
            r=struct.unpack_from('<6iII',data,offset);assert r[6]==0
            assert n==32+r[7]*(40 if typ==3 else 80)
            if typ==2:regions.append(r[:3]);known_empty+=r[7]==0
        offset+=n
    assert offset==len(data) and len(regions)==len(set(regions)) and known_empty
    voxels=p.raster([[[-1,100,-1],[-1,100,1],[0,101,1]]])
    assert voxels and all(x<=0 for x,y,z in voxels),'No occupancy under the unmapped half'
    for key,value in (('support_tolerance',.04),('feet_y_relative',[-1,33]),('held_out_failures',[{'error':.031}])):
        invalid={**source,key:value}
        try:p.pack(invalid)
        except AssertionError:pass
        else:raise AssertionError('Accepted invalid '+key)
    out=pathlib.Path(__file__).resolve().parent/'build/finite-check.bin';out.write_bytes(data)
    print('FPC2 complete regions/known empty/slope/hole/raster bounds pass; synthetic offline fixture')

if __name__=='__main__':main()
