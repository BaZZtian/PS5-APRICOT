// SPDX-License-Identifier: GPL-3.0-or-later
// Native homogeneous clipping and GPU packet preparation, no CPU rasterizer.
#pragma once
#include "geometry_state.h"
#include <cmath>
struct GeometryGpuVertex {float x,y,invw,z,s,t,r,g,b;};
struct GeometryGpuTriangle {GeometryGpuVertex vertex[3];uint32_t attributes,texture,palette,id;float bounds[4];};
static_assert(sizeof(GeometryGpuVertex)==36&&sizeof(GeometryGpuTriangle)==140,"GPU triangle layout");
struct GeometryClipVertex {double clip[4],st[2],rgb[3];};
static inline bool prepare_geometry_gpu(const std::vector<uint8_t> &raw,std::vector<uint8_t> &out,const char *&error) {
    error="invalid geometry snapshot";
    auto read=[&](size_t at)->uint32_t {uint32_t v=0;if(at>raw.size()||raw.size()-at<4)return 0;for(unsigned b=0;b<4;++b)v|=uint32_t(raw[at+b])<<(8*b);return v;};
    auto inside=[&](uint64_t at,uint64_t size) {return at<=raw.size()&&size<=raw.size()-at;};
    if(raw.size()<64)return false;
    uint32_t count=read(0),polygons=read(4),banks=read(8),vram=read(12),settings=read(16);
    if(count>2048||read(28)!=raw.size()||read(32)!=256||read(36)!=192||
       !inside(polygons,uint64_t(count)*132)||!inside(banks,9)||!inside(vram,0xa4000)||!inside(settings,160))return false;
    struct Group {std::vector<GeometryGpuTriangle> triangles;bool translucent;double ymin,ymax;};
    std::vector<Group> groups;uint32_t unsupported=0;
    for(unsigned i=0;i<count;++i) {
        GeometryPolygon polygon;std::memcpy(&polygon,raw.data()+polygons+i*132,132);
        if(polygon.count!=3&&polygon.count!=4)return false;
        std::vector<GeometryClipVertex> vertices;
        for(unsigned n=0;n<polygon.count;++n) {
            GeometryClipVertex v{};
            for(unsigned c=0;c<4;++c)v.clip[c]=polygon.vertex[n].clip[c]/4096.0;
            for(unsigned c=0;c<2;++c)v.st[c]=polygon.vertex[n].st[c]/16.0;
            for(unsigned c=0;c<3;++c) {unsigned color=(polygon.vertex[n].rgb>>(c*5))&31;v.rgb[c]=color?color*2+1:0;}
            vertices.push_back(v);
        }
        bool beyond_far=false;for(const auto &v:vertices)beyond_far|=v.clip[2]>v.clip[3];
        if(beyond_far&&!(polygon.attributes&(1u<<12)))continue;
        for(unsigned plane=0;plane<6&&!vertices.empty();++plane) {
            auto distance=[&](const GeometryClipVertex &v) {return v.clip[3]+(plane&1?-v.clip[plane/2]:v.clip[plane/2]);};
            std::vector<GeometryClipVertex> clipped;
            auto previous=vertices.back();double dp=distance(previous);
            for(const auto &current:vertices) {
                double dc=distance(current);
                if((dc>=0)!=(dp>=0)) {
                    double t=dp/(dp-dc);GeometryClipVertex v{};
                    for(unsigned c=0;c<4;++c)v.clip[c]=previous.clip[c]+(current.clip[c]-previous.clip[c])*t;
                    for(unsigned c=0;c<2;++c)v.st[c]=previous.st[c]+(current.st[c]-previous.st[c])*t;
                    for(unsigned c=0;c<3;++c)v.rgb[c]=previous.rgb[c]+(current.rgb[c]-previous.rgb[c])*t;
                    clipped.push_back(v);
                }
                if(dc>=0)clipped.push_back(current);
                previous=current;dp=dc;
            }
            vertices=std::move(clipped);
        }
        if(vertices.size()<3)continue;
        std::vector<GeometryGpuVertex> projected;
        unsigned x1=polygon.viewport&255,y1=(polygon.viewport>>8)&255,x2=(polygon.viewport>>16)&255,y2=polygon.viewport>>24;
        if(x2<x1||y2<y1) {error="wrapped viewport unsupported";return false;}
        for(const auto &v:vertices) {
            if(v.clip[3]<=0)continue;
            GeometryGpuVertex p{};p.invw=float(1.0/v.clip[3]);
            p.x=float((v.clip[0]/v.clip[3]+1)*(x2-x1+1)/2+x1);
            p.y=float(191-((v.clip[1]/v.clip[3]+1)*(y2-y1+1)/2+y1));
            p.z=float(v.clip[2]/v.clip[3]*0.5+0.5);
            p.s=float(v.st[0]/v.clip[3]);p.t=float(v.st[1]/v.clip[3]);
            p.r=float(v.rgb[0]/v.clip[3]);p.g=float(v.rgb[1]/v.clip[3]);p.b=float(v.rgb[2]/v.clip[3]);projected.push_back(p);
        }
        if(projected.size()<3)continue;
        unsigned alpha=(polygon.attributes>>16)&31,format=(polygon.texture>>26)&7;
        Group group{{},(alpha&&alpha<31)||format==1||format==6,1e30,-1e30};
        for(const auto &v:projected) {group.ymin=std::min(group.ymin,double(v.y));group.ymax=std::max(group.ymax,double(v.y));}
        bool has_area=false;
        for(unsigned n=1;n+1<projected.size();++n) {
            GeometryGpuTriangle t{};t.vertex[0]=projected[0];t.vertex[1]=projected[n];t.vertex[2]=projected[n+1];
            auto &a=t.vertex[0];auto &b=t.vertex[1];auto &c=t.vertex[2];double area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
            if(std::abs(area)<1e-10)continue; // Degenerate line/dot primitives still need a dedicated path.
            has_area=true;
            if(!(polygon.attributes&(1u<<(area<0?7:6))))continue;
            if(area<0)std::swap(t.vertex[1],t.vertex[2]);
            unsigned edges=(n==1?1:0)|2|(n+2==projected.size()?4:0);
            if(area<0)edges=((edges&1)<<2)|(edges&2)|((edges&4)>>2);
            t.attributes=polygon.attributes;t.texture=polygon.texture;t.palette=polygon.palette;t.id=i|(edges<<16);
            t.bounds[0]=std::min({a.x,b.x,c.x});t.bounds[1]=std::min({a.y,b.y,c.y});
            t.bounds[2]=std::max({a.x,b.x,c.x});t.bounds[3]=std::max({a.y,b.y,c.y});group.triangles.push_back(t);
        }
        if(!has_area) {
            // Collinear polygons are hardware line/dot primitives, regardless
            // of front/back culling. Expand them into a one-pixel GPU strip.
            unsigned first=0,second=0;double longest=0;
            for(unsigned a=0;a<projected.size();++a)for(unsigned b=a+1;b<projected.size();++b) {
                double dx=projected[a].x-projected[b].x,dy=projected[a].y-projected[b].y,d=dx*dx+dy*dy;
                if(d>longest) {longest=d;first=a;second=b;}
            }
            auto a=projected[first],b=projected[second];
            bool dot=std::floor(a.x)==std::floor(b.x)&&std::floor(a.y)==std::floor(b.y);
            if(dot&&!(polygon.attributes&0x2000)) {
                double limit=(raw[settings]|unsigned(raw[settings+1])<<8)/8.0;bool visible=false;
                for(const auto &v:vertices)visible|=v.clip[3]<=limit;
                if(!visible)continue;
            }
            GeometryGpuVertex rectangle[4];
            if(dot) {
                a=projected[0];double x=std::floor(a.x),y=std::floor(a.y);
                for(auto &p:rectangle)p=a;
                rectangle[0].x=rectangle[3].x=float(x);rectangle[1].x=rectangle[2].x=float(x+1);
                rectangle[0].y=rectangle[1].y=float(y);rectangle[2].y=rectangle[3].y=float(y+1);
            } else {
                a.x=std::floor(a.x)+0.5f;a.y=std::floor(a.y)+0.5f;b.x=std::floor(b.x)+0.5f;b.y=std::floor(b.y)+0.5f;
                double dx=b.x-a.x,dy=b.y-a.y,length=std::sqrt(dx*dx+dy*dy),nx=-dy/length*0.5,ny=dx/length*0.5;
                a.x-=float(dx/length*0.5);a.y-=float(dy/length*0.5);b.x+=float(dx/length*0.5);b.y+=float(dy/length*0.5);
                rectangle[0]=rectangle[3]=a;rectangle[1]=rectangle[2]=b;
                rectangle[0].x+=float(nx);rectangle[0].y+=float(ny);rectangle[1].x+=float(nx);rectangle[1].y+=float(ny);
                rectangle[2].x-=float(nx);rectangle[2].y-=float(ny);rectangle[3].x-=float(nx);rectangle[3].y-=float(ny);
            }
            for(unsigned n=0;n<2;++n) {
                GeometryGpuTriangle t{};t.vertex[0]=rectangle[0];t.vertex[1]=rectangle[n+1];t.vertex[2]=rectangle[n+2];
                auto &x=t.vertex[0];auto &y=t.vertex[1];auto &z=t.vertex[2];bool reverse=(y.x-x.x)*(z.y-x.y)-(y.y-x.y)*(z.x-x.x)<0;
                unsigned edges=(n==0?1:0)|2|(n==1?4:0);if(reverse) {std::swap(y,z);edges=((edges&1)<<2)|(edges&2)|((edges&4)>>2);}
                t.attributes=polygon.attributes;t.texture=polygon.texture;t.palette=polygon.palette;t.id=i|(edges<<16);
                t.bounds[0]=std::min({x.x,y.x,z.x});t.bounds[1]=std::min({x.y,y.y,z.y});t.bounds[2]=std::max({x.x,y.x,z.x});t.bounds[3]=std::max({x.y,y.y,z.y});
                group.triangles.push_back(t);
            }
        }
        if(!group.triangles.empty())groups.push_back(std::move(group));
    }
    // Stable transparent grouping. The provisional Y sort retains submission order on ties.
    bool manual=read(24)&1;
    std::stable_sort(groups.begin(),groups.end(),[&](const Group &a,const Group &b) {
        if(a.translucent!=b.translucent)return !a.translucent;
        if(!a.translucent||manual)return false;
        if(a.ymax!=b.ymax)return a.ymax<b.ymax;return a.ymin<b.ymin;
    });
    std::vector<GeometryGpuTriangle> triangles;for(const auto &group:groups)triangles.insert(triangles.end(),group.triangles.begin(),group.triangles.end());
    if(triangles.size()>16384) {error="clipped geometry limit exceeded";return false;}
    uint32_t triangle_at=64,banks_at=triangle_at+uint32_t(triangles.size())*140,vram_at=banks_at+12,settings_at=vram_at+0xa4000,total=settings_at+160;
    out.assign(total,0);uint32_t header[11]={uint32_t(triangles.size()),triangle_at,banks_at,vram_at,settings_at,read(20),read(24),total,256,192,unsupported};
    for(unsigned n=0;n<11;++n)for(unsigned b=0;b<4;++b)out[n*4+b]=uint8_t(header[n]>>(8*b));
    if(!triangles.empty())std::memcpy(out.data()+triangle_at,triangles.data(),triangles.size()*140);
    std::memcpy(out.data()+banks_at,raw.data()+banks,9);std::memcpy(out.data()+vram_at,raw.data()+vram,0xa4000);
    std::memcpy(out.data()+settings_at,raw.data()+settings,160);error="none";return true;
}
