"""Pack the independently surveyed finite patch for the native v11 producer."""
import argparse,collections,itertools,json,math,pathlib,struct

def raster(triangles):
    """Conservative SAT surface voxels, following SkyCraft Collision.cpp.

    See LICENSE-SkyCraft. Only queried triangle surfaces are occupied; no
    extrapolated solid half-space or supporting floor is added to empty areas.
    """
    blocks={};work=0
    def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
    def dot(a,b):return sum(x*y for x,y in zip(a,b))
    units=((1,0,0),(0,1,0),(0,0,1))
    for tri in triangles:
        vertices=[[x*8 for x in v] for v in tri]
        edges=[[vertices[(j+1)%3][k]-vertices[j][k] for k in range(3)] for j in range(3)]
        normal=cross(edges[0],edges[1]);assert dot(normal,normal)>1e-12
        axes=[*units,normal,*[cross(edge,unit) for edge in edges for unit in units]]
        projections=[([dot(v,axis) for v in vertices],.5*sum(abs(x) for x in axis),axis) for axis in axes]
        lo=[min(v[k] for v in vertices) for k in range(3)];hi=[max(v[k] for v in vertices) for k in range(3)]
        dom=max(range(3),key=lambda k:abs(normal[k]));u=(dom+1)%3;v=(dom+2)%3
        plane=dot(normal,vertices[0]);radius=.5*sum(abs(x) for x in normal)
        # ponytail: offline finite raster only, 8M candidate ceiling; reuse the
        # host voxel worker if this ever becomes a streaming terrain producer.
        for iu in range(math.floor(lo[u]),math.floor(hi[u])+1):
            for iv in range(math.floor(lo[v]),math.floor(hi[v])+1):
                q=[(plane+r-normal[u]*(iu+.5)-normal[v]*(iv+.5))/normal[dom] for r in (-radius,radius)]
                begin=max(math.floor(lo[dom]),math.floor(min(q)-.5));end=min(math.floor(hi[dom]),math.ceil(max(q)-.5))
                for idom in range(begin,end+1):
                    work+=1;assert work<=8_000_000,'Finite raster work budget exceeded'
                    point=[0,0,0];point[dom]=idom;point[u]=iu;point[v]=iv;center=[x+.5 for x in point]
                    if any(min(ps)-dot(center,axis)>r+1e-7 or max(ps)-dot(center,axis)<-r-1e-7 for ps,r,axis in projections):continue
                    cell=tuple(x//8 for x in point);bits=blocks.setdefault(cell,[0]*8)
                    bits[point[1]%8]|=1<<((point[2]%8)*8+point[0]%8)
                    assert len(blocks)<=65536,'Finite occupancy memory budget exceeded'
    return blocks

def pack_extended(source):
    assert source['support_tolerance']==.03 and 0<source['scale']<10
    assert not source.get('held_out_failures'), 'Failed independent validation cannot be published'
    assert 0<len(source['triangles'])<=16384
    assert all(0<=t['error']<=.03 and t.get('flags',0)==0 for t in source['triangles'])
    origin=source['origin_mc'];anchor=source['anchor'];scale=source['scale'];bounds=source['bounds_xz_relative'];vertical=source['feet_y_relative']
    values=[scale,*origin,*anchor,*bounds,*vertical];assert all(math.isfinite(v) and abs(v)<=100000 for v in values)
    assert 1<bounds[1]-bounds[0]<=64 and 1<bounds[3]-bounds[2]<=64 and -32<=vertical[0]<vertical[1]<=32
    triangles=[[[float((v[k]-anchor[k])/scale+origin[k]) for k in range(3)] for v in t['vertices']] for t in source['triangles']]
    low=[origin[0]+bounds[0]/scale-1,origin[1]+vertical[0]/scale-1,origin[2]+bounds[2]/scale-1]
    high=[origin[0]+bounds[1]/scale+1,origin[1]+vertical[1]/scale+4,origin[2]+bounds[3]/scale+1]
    for k in range(3):
        low[k]=min(low[k],min(v[k] for t in triangles for v in t));high[k]=max(high[k],max(v[k] for t in triangles for v in t))
    mins=[math.floor(x/8) for x in low];maxs=[math.floor(x/8) for x in high]
    dimensions=[b-a+1 for a,b in zip(mins,maxs)];assert all(0<n<=8 for n in dimensions) and math.prod(dimensions)<=256
    region_triangles=collections.defaultdict(list)
    for t in triangles:
        begin=[math.floor(min(v[k] for v in t)/8) for k in range(3)];end=[math.floor(max(v[k] for v in t)/8) for k in range(3)]
        for key in itertools.product(*(range(a,b+1) for a,b in zip(begin,end))):region_triangles[key].append(t)
    assert sum(map(len,region_triangles.values()))<=65536
    occupancy=collections.defaultdict(list)
    for cell,bits in raster(triangles).items():occupancy[tuple(x//8 for x in cell)].append(struct.pack('<3iI8Q',*cell,0,*bits))
    records=[(1,struct.pack('<I',0))]
    for key in itertools.product(*(range(a,b+1) for a,b in zip(mins,maxs))):
        lo=[v*8 for v in key];hi=[v+7 for v in lo];selected=region_triangles[key];blocks=occupancy[key]
        records.append((3,struct.pack('<6iII',*lo,*hi,0,len(selected))+b''.join(struct.pack('<9fI',*(x for v in t for x in v),0) for t in selected)))
        records.append((2,struct.pack('<6iII',*lo,*hi,0,len(blocks))+b''.join(blocks)))
    envelope=[v*8 for v in mins]+[v*8+7 for v in maxs]
    data=struct.pack('<4sI13f6i',b'FPC2',len(records),*values,*envelope)
    for typ,payload in records:
        assert len(payload)<=1<<20;data+=struct.pack('<II',typ,len(payload))+payload
    assert len(data)<=8<<20
    return data

def surface(triangles,x,z,axes=(0,2,1)):
    heights=[]
    for a,b,c in triangles:
        a,b,c=([v[i] for i in axes] for v in (a,b,c))
        den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-9:continue
        u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(z-c[1]))/den
        v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(z-c[1]))/den
        if min(u,v,1-u-v)>=-1e-6:heights.append(u*a[2]+v*b[2]+(1-u-v)*c[2])
    return max(heights) if heights else None

def pack(source):
    if 'feet_y_relative' in source:return pack_extended(source)
    assert source['support_tolerance']==.03 and 0<source['scale']<10
    assert all(t['error']<=.03 for t in source['triangles'])
    origin=source['origin_mc'];anchor=source['anchor'];scale=source['scale']
    triangles=[[[float((v[i]-anchor[i])/scale+origin[i]) for i in range(3)] for v in t['vertices']] for t in source['triangles']]
    floors=[t for t,s in zip(triangles,source['triangles']) if s.get('kind','floor')=='floor']
    walls=[t for t,s in zip(triangles,source['triangles']) if s.get('kind')=='wall']
    xmin,xmax,zmin,zmax=source['bounds_xz_relative']
    def inside(x,z):return xmin<=(x-origin[0])*scale<=xmax and zmin<=(z-origin[2])*scale<=zmax
    records=[(1,struct.pack('<I',0))]
    for rx in (-1,0):
        for ry in (11,12,13):
            for rz in (-1,0):
                lo=[v*8 for v in (rx,ry,rz)];hi=[v+7 for v in lo]
                selected=[t for t in triangles if all(max(v[i] for v in t)>=lo[i] and min(v[i] for v in t)<=hi[i]+1 for i in range(3))]
                tris=b''.join(struct.pack('<9fI',*(x for v in t for x in v),0) for t in selected)
                records.append((3,struct.pack('<6iII',*lo,*hi,0,len(selected))+tris))
                blocks=[]
                for x in range(lo[0],hi[0]+1):
                    for z in range(lo[2],hi[2]+1):
                        heights=[[surface(floors,x+(sx+.5)/8,z+(sz+.5)/8) for sx in range(8)] for sz in range(8)]
                        for y in range(max(98,lo[1]),min(104,hi[1]+1)):
                            bits=[]
                            for sy in range(8):
                                cy=y+(sy+.5)/8
                                wall=[surface(walls,x+(sx+.5)/8,cy,(0,1,2)) for sx in range(8)]
                                bits.append(sum(1<<(sz*8+sx) for sz in range(8) for sx in range(8)
                                    if inside(x+(sx+.5)/8,z+(sz+.5)/8) and
                                    ((heights[sz][sx] is not None and cy<heights[sz][sx]) or
                                     (wall[sx] is not None and z+(sz+.5)/8>=wall[sx]))))
                            if any(bits):blocks.append(struct.pack('<3iI8Q',x,y,z,0,*bits))
                records.append((2,struct.pack('<6iII',*lo,*hi,0,len(blocks))+b''.join(blocks)))
    # Local file container only; the messages themselves are unchanged protocol11.
    data=struct.pack('<4sI11f',b'FPC1',len(records),scale,*origin,*anchor,*source['bounds_xz_relative'])
    for typ,payload in records:data+=struct.pack('<II',typ,len(payload))+payload
    assert len(records)==25 and len(data)<1024*1024
    return data

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('survey',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
    data=pack(json.loads(a.survey.read_text()));a.output.write_bytes(data);print(len(data),'bytes')
