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
    if(positionTexture) positionTexture->surface_set(nullptr);
    if(colorTexture) colorTexture->surface_set(nullptr);
    for(int bank=0;bank<2;++bank) { basePosition[bank].destroy();baseColor[bank].destroy(); }
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
    if (!level->ObjectSpace.RayPick(from,down,80.f,collide::rqtStatic,result,nullptr)) return;
    const CDB::TRI& triangle=level->ObjectSpace.GetStaticTris()[result.element];
    if (triangle.material>=GMLib.CountMaterial()) return;
    const SGameMtl* material=GMLib.GetMaterialByIdx(triangle.material);
    if (material->Flags.test(SGameMtl::flPassable)) return;
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
    if(memcmp(&requested,&parameters,sizeof(parameters))) { parameters=requested; ++fieldRevision; }
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
    centerX=x; centerZ=z;
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
        Probe(cx,cz,Device.vCameraPosition.y); ++queries;
    }
    Contact contacts[256];
    const unsigned count=Touches().Drain(contacts);
    for (unsigned i=0;i<count;++i)
    {
        const Contact& contact=contacts[i];
        const int cx=int(std::floor(contact.x/step)),cz=int(std::floor(contact.z/step));
        const int reach=std::min(4,int(std::ceil(contact.radius/step))+1);
        for (int dz=-reach;dz<=reach;++dz) for (int dx=-reach;dx<=reach;++dx)
        {
            Sample& cell=Cell(cx+dx,cz+dz);
            if (!cell.known || !cell.valid || cell.x!=cx+dx || cell.z!=cz+dz ||
                contact.y<cell.y-.15f || contact.y>cell.y+.45f) continue;
            const float sx=cell.x*step-contact.x, sz=cell.z*step-contact.z;
            const float strength=Smooth(1.f-std::sqrt(sx*sx+sz*sz)/(contact.radius+step*.5f));
            cell.remaining=std::min(cell.remaining,1.f-.85f*strength);
        }
    }
    Rebuild();
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
        const float dx=cx*step-Device.vCameraPosition.x,dz=cz*step-Device.vCameraPosition.z;
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

void CSnowRenderer::CopyGround()
{
    auto* target=RImplementation.Target;
    const int bank=Device.m_SecondViewport.IsSVPFrame()?1:0;
    const ref_rt source[2]={target->rt_Position,RImplementation.o.albedo_wo?target->rt_Accumulator:target->rt_Color};
    ref_rt* copies[2]={&basePosition[bank],&baseColor[bank]};
    if(!positionTexture) positionTexture.create("$user$snow_base_position");
    if(!colorTexture) colorTexture.create("$user$snow_base_color");
    RCache.set_Textures(nullptr);
    positionTexture->surface_set(nullptr);colorTexture->surface_set(nullptr);
    for(unsigned i=0;i<2;++i)
    {
        ref_rt& copy=*copies[i];
        if(copy && (copy->dwWidth!=source[i]->dwWidth || copy->dwHeight!=source[i]->dwHeight ||
            copy->fmt!=source[i]->fmt || copy->sampleCount!=source[i]->sampleCount)) copy.destroy();
        if(!copy)
        {
            string64 name;xr_sprintf(name,"$user$snow_copy_%d_%u",bank,i);
            copy.create(name,source[i]->dwWidth,source[i]->dwHeight,source[i]->fmt,source[i]->sampleCount);
        }
        HW.pContext->CopyResource(copy->pSurface,source[i]->pSurface);
    }
    positionTexture->surface_set(basePosition[bank]->pSurface);colorTexture->surface_set(baseColor[bank]->pSurface);
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
    CopyGround();
    if (!shader)
    {
        shader.create("anthology_snow_volume");
    }
    if(!geometry) CreateGeometry();
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
    RCache.set_xform_world(Fidentity);RCache.set_Shader(shader);RCache.set_Geometry(geometry);
    RCache.set_CullMode(CULL_NONE);
    static shared_str field("anthology_snow_field");
    RCache.set_c(field,Get(Height),Get(Density),Get(Variation),Get(DriftSize));
    const PipMotionHistoryScope history(nullptr,true);
    RCache.Render(D3DPT_TRIANGLELIST,0,0,u32(vertices.size()),0,u32(indices.size()/3));
    RCache.set_CullMode(CULL_CCW);
}
