#include "stdafx.h"
#include "SnowRenderer.h"
#include "../../xrCore/SnowField.h"
#include "../../xrEngine/IGame_Level.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/GameMtlLib.h"
#include "../xrRender/PipMotionHistory.h"

using namespace anthology::snow;

void CSnowRenderer::Clear()
{
    geometry.destroy();
    if(vertexBuffer) { HW.stats_manager.decrement_stats_vb(vertexBuffer);_RELEASE(vertexBuffer); }
    if(indexBuffer) { HW.stats_manager.decrement_stats_ib(indexBuffer);_RELEASE(indexBuffer); }
    if(trackTexture) trackTexture->surface_set(nullptr);
    _RELEASE(trackSurface);
    tracks.clear(); tracksDirty=false; meshDirty=true; settleHistory=false;
    uploadedRevision=u32(-1); uploadedBank=-1;
    if (enabled) Contacts().Clear(step);
    Contact discarded[256]; Touches().Drain(discarded);
    enabled = false;
    samples.clear(); offsets.clear(); vertices.clear(); indices.clear();
    level = nullptr; distance = 0.f; probe = 0; updateTime = u32(-1);
}

void CSnowRenderer::Probe(int x, int z, float cameraY)
{
    Sample& cell = Cell(x,z);
    const bool retained=cell.known && cell.valid && cell.x==x && cell.z==z;
    const float oldY=cell.y,oldRemaining=cell.remaining;
    cell.x=x; cell.z=z; cell.known=true; cell.valid=false; cell.remaining=1.f;cell.publishedRemaining=-1.f;cell.fieldRevision=0;cell.scopeHistory=false;
    cell.probeHeight=cameraY;
    Contacts().Publish(x,z,0.f,0.f);
    for(int dz=-2;dz<=2;++dz) for(int dx=-2;dx<=2;++dx) Cell(x+dx,z+dz).edgeDirty=true;
    collide::rq_result result;
    Fvector from={x*step,cameraY+3.f,z*step}, down={0,-1,0};
    // Passable leaves/grass are not the ground. Continue through them, bounded.
    const CDB::TRI* hit=nullptr;
    const SGameMtl* material=nullptr;
    for(unsigned layer=0;layer<16;++layer)
    {
        if (!level->ObjectSpace.RayPick(from,down,80.f,collide::rqtStatic,result,nullptr)) return;
        hit=&level->ObjectSpace.GetStaticTris()[result.element];
        if(hit->material>=GMLib.CountMaterial()) return;
        material=GMLib.GetMaterialByIdx(hit->material);
        if(!material->Flags.test(SGameMtl::flPassable)) break;
        from.y-=result.range+.02f; material=nullptr;
    }
    if(!material) return;
    const CDB::TRI& triangle=*hit;
    const char* name=material->m_Name.c_str();
    // Water/ice and vertical scenery retain their own seasonal material.
    if (strstr(name,"water") || strstr(name,"ice") || strstr(name,"metal") ||
        strstr(name,"wood") || strstr(name,"glass")) return;
    Fvector* points=level->ObjectSpace.GetStaticVerts();
    cell.normal.mknormal(points[triangle.verts[0]],points[triangle.verts[1]],points[triangle.verts[2]]);
    if (cell.normal.y<.45f) return;
    cell.y=from.y-result.range;
    Fvector up={0,1,0}, roofStart={from.x,cell.y+.08f,from.z};
    for(unsigned layer=0;layer<8;++layer)
    {
        if (!level->ObjectSpace.RayPick(roofStart,up,80.f,collide::rqtStatic,result,nullptr)) break;
        const auto& roof=level->ObjectSpace.GetStaticTris()[result.element];
        if(roof.material>=GMLib.CountMaterial() || !GMLib.GetMaterialByIdx(roof.material)->Flags.test(SGameMtl::flPassable)) return;
        roofStart.y+=result.range+.03f;
    }
    cell.valid=true;
    if(retained && abs(cell.y-oldY)<.02f) cell.remaining=oldRemaining;
    cell.previousY=cell.y+Depth(from.x,from.z)*Smooth((cell.normal.y-.45f)/.35f)+.003f;
}

void CSnowRenderer::Update()
{
    if (updateTime==Device.dwFrame) return;
    updateTime=Device.dwFrame;
    const float desiredDistance=Get(Distance);
    Fvector4 requested;requested.set(Get(Height),Get(Density),Get(Variation),Get(DriftSize));
    if(memcmp(&requested,&parameters,sizeof(parameters))) { parameters=requested; ++fieldRevision; meshDirty=true; }
    if (level!=g_pGameLevel || distance!=desiredDistance || samples.empty())
    {
        Clear(); enabled=true; level=g_pGameLevel; distance=desiredDistance;
        step=std::max(.25f,distance/100.f);
        radius=std::min(100,int(std::ceil(distance/step)));
        samples.resize(Side*Side);
        Contacts().Clear(step);
        for (int z=-radius;z<=radius;++z) for(int x=-radius;x<=radius;++x)
            if(x*x+z*z<=(radius+1)*(radius+1)) offsets.push_back(Fvector2().set(float(x),float(z)));
        std::sort(offsets.begin(),offsets.end(),[](const Fvector2& a,const Fvector2& b) {
            return a.x*a.x+a.y*a.y < b.x*b.x+b.y*b.y;
        });
        updateTime=Device.dwFrame;
    }
    const int x=int(std::floor(Device.vCameraPosition.x/step)), z=int(std::floor(Device.vCameraPosition.z/step));
    if (abs(x-centerX)>radius || abs(z-centerZ)>radius) probe=0;
    if(x!=centerX || z!=centerZ) meshDirty=true;
    centerX=x; centerZ=z;
    UpdateTracks();
    if (probe>=offsets.size()) probe=0;
    // At most 64 new cells and 0.4 ms of collision work per main frame.
    const u64 began=CPU::QPC(); unsigned queries=0, visited=0;
    while (probe<offsets.size() && queries<64 && visited<512)
    {
        if (queries && double(CPU::QPC()-began)*1000.0/double(CPU::qpc_freq)>.4) break;
        const Fvector2& offset=offsets[probe++]; ++visited;
        const int cx=centerX+int(offset.x), cz=centerZ+int(offset.y);
        Sample& cell=Cell(cx,cz);
        if (cell.known && cell.x==cx && cell.z==cz && abs(cell.probeHeight-Device.vCameraPosition.y)<2.f) continue;
        Probe(cx,cz,Device.vCameraPosition.y); ++queries; meshDirty=true;
    }
    Contact contacts[256];
    const unsigned count=Touches().Drain(contacts);
    for (unsigned i=0;i<count;++i)
    {
        const Contact& contact=contacts[i];
        if(abs(contact.x-trackX*TrackStep)>TrackSide*TrackStep*.5f ||
            abs(contact.z-trackZ*TrackStep)>TrackSide*TrackStep*.5f) continue;
        StampTrack(contact.x,contact.y,contact.z,contact.radius,contact.forwardX,contact.forwardZ,contact.bullet);
        const int cx=int(std::floor(contact.x/step)),cz=int(std::floor(contact.z/step));
        const int reach=std::min(4,int(std::ceil(contact.radius/step))+1);
        for (int dz=-reach;dz<=reach;++dz) for (int dx=-reach;dx<=reach;++dx)
        {
            Sample& cell=Cell(cx+dx,cz+dz);
            if (!cell.known || !cell.valid || cell.x!=cx+dx || cell.z!=cz+dz ||
                contact.y<cell.y-.15f || contact.y>cell.y+.45f) continue;
            const float sx=cell.x*step-contact.x, sz=cell.z*step-contact.z;
            const float strength=Smooth(1.f-std::sqrt(sx*sx+sz*sz)/(contact.radius+step*.5f));
            // Fine sole/crater shading is independent of coarse grid spacing.
            const float remaining=std::min(cell.remaining,1.f-.30f*strength);
            if(remaining!=cell.remaining) { cell.remaining=remaining; meshDirty=true; }
        }
    }
    // Rebuild only on changed geometry; one settling update clears deformation MV.
    if(meshDirty || settleHistory)
    {
        const bool changed=meshDirty;
        Rebuild(); ++meshRevision;
        meshDirty=false; settleHistory=changed;
    }
}

bool CSnowRenderer::Valid(int x,int z,float ground)
{
    const Sample& cell=Cell(x,z);
    return cell.known && cell.valid && cell.x==x && cell.z==z && abs(cell.y-ground)<=step*2.f;
}

void CSnowRenderer::Rebuild()
{
    const int side=radius*2+1;
    vertices.resize(side*side); indices.clear();
    for(int z=0;z<side;++z) for(int x=0;x<side;++x)
    {
        const int cx=centerX+x-radius, cz=centerZ+z-radius;
        Sample& cell=Cell(cx,cz);
        Vertex& vertex=vertices[z*side+x];
        vertex.p.set(cx*step,cell.y,cz*step);vertex.n.set(0,1,0);vertex.data.set(0,cell.y,0,0);
        if (!cell.known || !cell.valid || cell.x!=cx || cell.z!=cz) continue;
        const float dx=(cx-centerX)*step,dz=(cz-centerZ)*step;
        const float fade=Smooth((distance-std::sqrt(dx*dx+dz*dz))/std::max(2.f,distance*.15f));
        const float slope=Smooth((cell.normal.y-.45f)/.35f);
        if(cell.edgeDirty)
        {
            cell.edge=1.f;
            for(int dz=-2;dz<=2;++dz) for(int dx=-2;dx<=2;++dx)
                if((dx || dz) && !Valid(cx+dx,cz+dz,cell.y))
                    cell.edge=std::min(cell.edge,Smooth((std::sqrt(float(dx*dx+dz*dz))-1.f)*.5f));
            cell.edgeDirty=false;
        }
        if(cell.fieldRevision!=fieldRevision)
        {
            cell.depth=Depth(vertex.p.x,vertex.p.z,parameters.x,parameters.y,parameters.z,parameters.w);
            cell.fieldRevision=fieldRevision;
        }
        vertex.n=cell.normal;
        vertex.data.set(cell.remaining,cell.previousY,slope,fade*cell.edge);
        cell.previousY=cell.y+cell.depth*cell.remaining*slope*fade*cell.edge+.003f;
        const float physicalRemaining=cell.remaining*slope*fade*cell.edge;
        if(cell.publishedRemaining!=physicalRemaining)
        {
            Contacts().Publish(cx,cz,cell.y,physicalRemaining);
            cell.publishedRemaining=physicalRemaining;
        }
    }
    // Smooth the lighting across adjacent displaced triangles, including tracks.
    for(int z=1;z<side-1;++z) for(int x=1;x<side-1;++x)
    {
        Vertex& v=vertices[z*side+x];
        if(v.data.z==0) continue;
        const int cx=centerX+x-radius,cz=centerZ+z-radius;
        if(!Valid(cx-1,cz,v.p.y) || !Valid(cx+1,cz,v.p.y) ||
            !Valid(cx,cz-1,v.p.y) || !Valid(cx,cz+1,v.p.y)) continue;
        const float dx=Cell(cx+1,cz).previousY-Cell(cx-1,cz).previousY;
        const float dz=Cell(cx,cz+1).previousY-Cell(cx,cz-1).previousY;
        v.n.set(-dx,2.f*step,-dz).normalize_safe();
    }
    for(int z=0;z<side-1;++z) for(int x=0;x<side-1;++x)
    {
        const u16 a=u16(z*side+x),b=a+1,c=a+u16(side),d=c+1;
        const Vertex& va=vertices[a]; const Vertex& vb=vertices[b];
        const Vertex& vc=vertices[c]; const Vertex& vd=vertices[d];
        if (va.data.z==0 || vb.data.z==0 || vc.data.z==0 || vd.data.z==0) continue;
        if (std::max(std::max(va.data.w,vb.data.w),std::max(vc.data.w,vd.data.w))<=0.f) continue;
        const float lo=std::min(std::min(va.p.y,vb.p.y),std::min(vc.p.y,vd.p.y));
        const float hi=std::max(std::max(va.p.y,vb.p.y),std::max(vc.p.y,vd.p.y));
        if (hi-lo>step*2.f) continue; // Do not bridge different floors or cliffs.
        indices.insert(indices.end(),{a,c,b,b,c,d});
    }
}

void CSnowRenderer::UpdateTracks()
{
    const int x=int(std::floor(Device.vCameraPosition.x/(TrackStep*8)))*8;
    const int z=int(std::floor(Device.vCameraPosition.z/(TrackStep*8)))*8;
    if(tracks.empty())
    {
        tracks.resize(TrackSide*TrackSide); trackX=x; trackZ=z; tracksDirty=true;
    }
    if(x==trackX && z==trackZ) return;
    // Only clear newly exposed strips in the toroidal world-aligned field.
    if(abs(x-trackX)>=TrackSide || abs(z-trackZ)>=TrackSide)
        std::fill(tracks.begin(),tracks.end(),TrackPixel{});
    else
    {
        const int xbegin=x>trackX?trackX+256:x-256, xend=x>trackX?x+256:trackX-256;
        const int zbegin=z>trackZ?trackZ+256:z-256, zend=z>trackZ?z+256:trackZ-256;
        for(int iz=z-256;iz<z+256;++iz) for(int ix=xbegin;ix<xend;++ix)
            tracks[(unsigned(ix)&511)+(unsigned(iz)&511)*TrackSide]={};
        for(int iz=zbegin;iz<zend;++iz) for(int ix=x-256;ix<x+256;++ix)
            tracks[(unsigned(ix)&511)+(unsigned(iz)&511)*TrackSide]={};
    }
    trackX=x; trackZ=z; tracksDirty=true;
}

void CSnowRenderer::StampTrack(float x,float y,float z,float radius,float fx,float fz,bool bullet)
{
    if(tracks.empty() || Contacts().At(x,y,z)<=.002f) return;
    const float length=std::sqrt(fx*fx+fz*fz);
    if(length>.001f) { fx/=length;fz/=length; } else { fx=0.f;fz=1.f; }
    const int cx=int(std::floor(x/TrackStep)),cz=int(std::floor(z/TrackStep));
    const int reach=int(std::ceil(radius/TrackStep))+1;
    for(int iz=cz-reach;iz<=cz+reach;++iz) for(int ix=cx-reach;ix<=cx+reach;++ix)
    {
        if(ix<trackX-256 || ix>=trackX+256 || iz<trackZ-256 || iz>=trackZ+256) continue;
        const float dx=(ix+.5f)*TrackStep-x,dz=(iz+.5f)*TrackStep-z;
        const float along=dx*fx+dz*fz,across=dx*fz-dz*fx;
        const float toe=(along-.045f)/.13f,heel=(along+.105f)/.07f;
        const float r=(bullet || radius>.2f) ? std::sqrt(dx*dx+dz*dz)/radius :
            std::min(std::sqrt(toe*toe+across*across/(.08f*.08f)),
                std::sqrt(heel*heel+across*across/(.062f*.062f)));
        const float strength=Smooth((1.f-r)*3.f)*(bullet?.9f:.7f);
        TrackPixel& pixel=tracks[(unsigned(ix)&511)+(unsigned(iz)&511)*TrackSide];
        if(strength>pixel.depth)
        {
            pixel.depth=strength; pixel.ground=y*strength; tracksDirty=true;
        }
    }
    if(strstr(Core.Params,"-snow_profile")) Msg("[snow-contact] %s %.3f %.3f %.3f",bullet?"bullet":(radius>.2f?"body":"foot"),x,y,z);
}

void CSnowRenderer::UploadTracks()
{
    if(!trackSurface)
    {
        D3D11_TEXTURE2D_DESC desc={};desc.Width=desc.Height=TrackSide;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R32G32_FLOAT;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        R_CHK(HW.pDevice->CreateTexture2D(&desc,nullptr,&trackSurface));
        trackTexture.create("$user$snow_tracks");trackTexture->surface_set(trackSurface);tracksDirty=true;
    }
    if(!tracksDirty) return;
    RCache.set_Textures(nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped={};
    R_CHK(HW.pContext->Map(trackSurface,0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
    for(unsigned y=0;y<TrackSide;++y)
        CopyMemory(static_cast<BYTE*>(mapped.pData)+y*mapped.RowPitch,tracks.data()+y*TrackSide,TrackSide*sizeof(TrackPixel));
    HW.pContext->Unmap(trackSurface,0);tracksDirty=false;
}

void CSnowRenderer::CreateGeometry()
{
    D3D11_BUFFER_DESC desc={};desc.Usage=D3D11_USAGE_DYNAMIC;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;desc.ByteWidth=201*201*sizeof(Vertex);
    R_CHK(HW.pDevice->CreateBuffer(&desc,nullptr,&vertexBuffer));HW.stats_manager.increment_stats_vb(vertexBuffer);
    desc.BindFlags=D3D11_BIND_INDEX_BUFFER;desc.ByteWidth=200*200*6*sizeof(u16);
    R_CHK(HW.pDevice->CreateBuffer(&desc,nullptr,&indexBuffer));HW.stats_manager.increment_stats_ib(indexBuffer);
    D3DVERTEXELEMENT9 declaration[]={
        {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},
        {0,24,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    geometry.create(declaration,vertexBuffer,indexBuffer);
}

void CSnowRenderer::Render()
{
    if (!Active() || !g_pGameLevel || !g_pGameLevel->bReady)
    {
        if(enabled) Clear();
        return;
    }
    if (!Device.m_SecondViewport.IsSVPFrame()) Update();
    if (indices.empty()) return;
    UploadTracks();
    if (!shader)
    {
        shader.create("anthology_snow_volume");
    }
    if(!geometry) CreateGeometry();
    const int bank=Device.m_SecondViewport.IsSVPFrame()?1:0;
    if(uploadedRevision!=meshRevision || uploadedBank!=bank || settleHistory)
    {
    D3D11_MAPPED_SUBRESOURCE mapped={};
    R_CHK(HW.pContext->Map(vertexBuffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
    void* data=mapped.pData;
    CopyMemory(data,vertices.data(),vertices.size()*sizeof(Vertex));
    if(Device.m_SecondViewport.IsSVPFrame())
    {
        Vertex* uploaded=static_cast<Vertex*>(data);
        const int side=radius*2+1;
        for(int z=0;z<side;++z) for(int x=0;x<side;++x)
        {
            Sample& cell=Cell(centerX+x-radius,centerZ+z-radius);
            if(uploaded[z*side+x].data.z==0) continue;
            uploaded[z*side+x].data.y=cell.scopeHistory?cell.previousScopeY:cell.previousY;
            cell.previousScopeY=cell.previousY;cell.scopeHistory=true;
        }
    }
    HW.pContext->Unmap(vertexBuffer,0);
    R_CHK(HW.pContext->Map(indexBuffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
    CopyMemory(mapped.pData,indices.data(),indices.size()*sizeof(u16));
    HW.pContext->Unmap(indexBuffer,0);
    uploadedRevision=meshRevision;uploadedBank=bank;
    }
    RCache.set_xform_world(Fidentity);RCache.set_Shader(shader);RCache.set_Geometry(geometry);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(TRUE,D3DCMP_ALWAYS,0x01,0xff,0x7f,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
    static shared_str field("anthology_snow_field");
    RCache.set_c(field,Get(Height),Get(Density),Get(Variation),Get(DriftSize));
    RCache.set_c("snow_track_field",trackX*TrackStep,trackZ*TrackStep,1.f/(TrackStep*TrackSide),TrackStep);
    const PipMotionHistoryScope history(nullptr,true);
    RCache.Render(D3DPT_TRIANGLELIST,0,0,u32(vertices.size()),0,u32(indices.size()/3));
    RCache.set_CullMode(CULL_CCW);
}
