// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace apricot {
// Private APCM0001 mesh. No game/model data is compiled into this renderer.
class CartridgeView {
    struct Vertex { float p[3], n[3], uv[2]; };
    struct Triangle { uint32_t v[3], material; };
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
    float middle[3]{}, extent=1;
    std::array<float,3> pivot{};
    static uint32_t u32(const uint8_t* p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
public:
    bool load(const std::vector<uint8_t>& bytes) {
        vertices.clear();triangles.clear();
        if(bytes.size()<32||memcmp(bytes.data(),"APCM0001",8))return false;
        uint32_t nv=u32(bytes.data()+8),nt=u32(bytes.data()+12),nm=u32(bytes.data()+16);
        if(!nv||nv>65536||!nt||nt>32768||!nm||nm>16||bytes.size()!=32+size_t(nv)*32+size_t(nt)*16)return false;
        for(unsigned i=20;i<32;++i)if(bytes[i])return false;
        std::vector<Vertex> v(nv);std::vector<Triangle> t(nt);
        float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
        for(unsigned i=0;i<nv;++i){
            float a[8];for(unsigned k=0;k<8;++k){uint32_t bits=u32(bytes.data()+32+i*32+k*4);memcpy(a+k,&bits,4);if(!std::isfinite(a[k])||std::abs(a[k])>10000)return false;}
            for(unsigned k=0;k<3;++k){v[i].p[k]=a[k];v[i].n[k]=a[k+3];low[k]=std::min(low[k],a[k]);high[k]=std::max(high[k],a[k]);}
            v[i].uv[0]=a[6];v[i].uv[1]=a[7];
        }
        for(unsigned i=0;i<nt;++i){auto p=bytes.data()+32+nv*32+i*16;for(unsigned k=0;k<3;++k){t[i].v[k]=u32(p+k*4);if(t[i].v[k]>=nv)return false;}t[i].material=u32(p+12);if(t[i].material>=nm)return false;}
        for(unsigned k=0;k<3;++k)middle[k]=(low[k]+high[k])*0.5f;
        extent=std::max({high[0]-low[0],high[1]-low[1],high[2]-low[2]});
        if(extent<0.001f)return false;
        // First stand the cartridge on its lowest physical corner, then spin
        // around the world vertical through that corner (not the local edge).
        pivot={0,INFINITY,0};constexpr float d=0.7071067812f;
        for(const auto& item:v){float x=(item.p[0]-middle[0])/extent,y=(item.p[2]-middle[2])/extent;
            float tx=d*(x-y),ty=d*(x+y),tz=(item.p[1]-middle[1])/extent;
            if(ty<pivot[1])pivot={tx,ty,tz};}
        vertices=std::move(v);triangles=std::move(t);return true;
    }
    bool ready()const{return !triangles.empty();}
    // Native triangle rasterizer for the small menu mesh, with perspective UVs,
    // lighting and a depth plane. Geometry is never substituted by a flat card.
    size_t draw(std::vector<uint32_t>& canvas,unsigned width,unsigned height,
                float cx,float cy,float size,double angle,bool gold,
                const std::vector<uint32_t>& label,bool output_bgra=false)const {
        if(!ready()||!width||!height||width>4096||height>2160||canvas.size()!=size_t(width)*height||
           !std::isfinite(cx)||!std::isfinite(cy)||std::abs(cx)>8192||std::abs(cy)>8192||!std::isfinite(size)||size<=0||size>4096||!std::isfinite(angle))return 0;
        struct P{float x,y,q,u,v,light;};std::vector<P> points;points.reserve(vertices.size());
        float s=std::sin(angle),c=std::cos(angle);
        constexpr float d=0.7071067812f;
        for(auto& v:vertices){
            float x=(v.p[0]-middle[0])/extent,y=(v.p[2]-middle[2])/extent,z=(v.p[1]-middle[1])/extent;
            float tx=d*(x-y)-pivot[0],ty=d*(x+y)-pivot[1],tz=z-pivot[2];
            float rx=c*tx+s*tz,rz=-s*tx+c*tz,q=1.f/(2.6f-rz);
            float nx=d*(v.n[0]-v.n[2]),ny=d*(v.n[0]+v.n[2]),nz=v.n[1];
            float light=0.58f+0.42f*std::max(0.f,0.25f*(c*nx+s*nz)+0.85f*(-s*nx+c*nz)+0.3f*ny);
            points.push_back({cx+rx*q*size*2.6f,cy-ty*q*size*2.6f,q,v.uv[0]*q,v.uv[1]*q,light});
        }
        // A diamond is taller than an edge-standing square. Clip to the exact
        // projected bounds; the corner at (cx,cy) stays fixed at every angle.
        float minx=cx,maxx=cx,miny=cy,maxy=cy;
        for(const auto& p:points){minx=std::min(minx,p.x);maxx=std::max(maxx,p.x);miny=std::min(miny,p.y);maxy=std::max(maxy,p.y);}
        int left=std::max(0,int(std::floor(minx))),right=std::min(int(width)-1,int(std::ceil(maxx)));
        int top=std::max(0,int(std::floor(miny))),bottom=std::min(int(height)-1,int(std::ceil(maxy)));
        if(right<left||bottom<top)return 0;
        unsigned stride=unsigned(right-left+1);std::vector<float> depth(size_t(stride)*(bottom-top+1),0);size_t written=0;
        auto edge=[](const P&a,const P&b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};
        const uint32_t silver[5]={0xff514737,0xff302a22,0xffb5b5ae,0xff66503a,0xffc0d0d0};
        const uint32_t golden[5]={0xff273f52,0xff1b2a36,0xff3c9fce,0xff365d7b,0xffc0d0d0};
        const uint32_t* colors=gold?golden:silver;
        for(const auto&t:triangles){
            const P &a=points[t.v[0]],&b=points[t.v[1]],&d=points[t.v[2]];float area=edge(a,b,d.x,d.y);if(std::abs(area)<0.0001f)continue;
            int x0=std::max(left,int(std::floor(std::min({a.x,b.x,d.x})))),x1=std::min(right,int(std::ceil(std::max({a.x,b.x,d.x}))));
            int y0=std::max(top,int(std::floor(std::min({a.y,b.y,d.y})))),y1=std::min(bottom,int(std::ceil(std::max({a.y,b.y,d.y}))));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
                float wa=edge(b,d,x+0.5f,y+0.5f)/area,wb=edge(d,a,x+0.5f,y+0.5f)/area,wc=1-wa-wb;
                if(wa<-0.00001f||wb<-0.00001f||wc<-0.00001f)continue;
                float q=wa*a.q+wb*b.q+wc*d.q;auto at=size_t(y-top)*stride+unsigned(x-left);if(q<=depth[at])continue;
                uint32_t color=colors[std::min(4u,t.material)];
                if(t.material==2&&gold)color=0xff3c9fce;
                if(t.material==4&&label.size()==256*192){float u=(wa*a.u+wb*b.u+wc*d.u)/q,v=(wa*a.v+wb*b.v+wc*d.v)/q;
                    unsigned tx=unsigned(std::clamp(u,0.f,1.f)*255),ty=unsigned(std::clamp(v,0.f,1.f)*191);color=label[ty*256+tx];}
                float light=t.material==4?1.f:std::clamp(wa*a.light+wb*b.light+wc*d.light,0.3f,1.15f);uint32_t lit=0xff000000;
                for(unsigned k=0;k<3;++k)lit|=uint32_t(std::min(255.f,float((color>>(k*8))&255)*light))<<(k*8);
                // Title captures are RGBA8, whereas the PS5 launcher canvas is BGRA8.
                if(output_bgra)lit=(lit&0xff00ff00u)|((lit&255u)<<16)|((lit>>16)&255u);
                canvas[size_t(y)*width+x]=lit;depth[at]=q;++written;
            }
        }
        return written;
    }
};
}
