// SPDX-License-Identifier: GPL-3.0-or-later
// Original matrix/vertex command adapter. Rendering is a separate native GPU job.
#pragma once
#include <stdint.h>
#include <array>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cmath>
#include "vram.h"
struct GeometryVertex {int32_t clip[4],st[2];uint32_t rgb;};
struct GeometryPolygon {GeometryVertex vertex[4];uint32_t count,attributes,texture,palette,viewport;};
static_assert(sizeof(GeometryVertex)==28&&sizeof(GeometryPolygon)==132,"Geometry packet layout");
struct HostGeometry {
    using Matrix=std::array<int32_t,16>;
    Matrix projection{},position{},direction{},texture{};
    Matrix projection_stack{},texture_stack{},position_stack[32]{},direction_stack[32]{};
    unsigned mode=0,position_level=0,projection_level=0;
    bool error=false,box_result=false;
    uint32_t irq_mode=0,attributes[256]{},parameters[32]{};
    uint8_t settings[0xa0]{};
    uint8_t commands[4]{};unsigned next_command=0,command_count=0,parameter_count=0;
    unsigned rejected_command=0;
    uint64_t empty_swaps=0;uint32_t swap_parameters=0,empty_clear_color=0;
    std::vector<GeometryPolygon> building,completed;
    GeometryVertex pending[4]{};unsigned primitive=0,vertex_count=0,primitive_vertices=0;
    uint32_t polygon_attributes=0,current_color=0,completed_parameters=0;
    int32_t coordinate[3]{},texcoord[2]{},transformed_texcoord[2]{},position_result[4]{},vector_result[3]{};
    int32_t light_vector[4][3]{},half_vector[4][3]{};
    bool begun=false,swap_pending=false,failed=false,strip_stored=false;uint64_t swaps=0,vertices=0;
    unsigned list_vertices=0;uint8_t completed_settings[0xa0]{},completed_banks[9]{};
    uint16_t completed_control=0;
    uint16_t completed_dot_depth=0;
    std::vector<uint8_t> completed_vram=std::vector<uint8_t>(0xa4000);
    struct Queued {unsigned command;std::array<uint32_t,32> words;};
    std::vector<Queued> deferred;unsigned deferred_words=0;
    static Matrix identity() {Matrix m{};for(unsigned n=0;n<4;++n)m[n*5]=4096;return m;}
    HostGeometry() {projection=position=direction=texture=identity();}
    static Matrix multiply(const Matrix &a,const Matrix &b) {
        Matrix out{};
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c) {
            __int128 sum=0;for(unsigned k=0;k<4;++k)sum+=__int128(a[r*4+k])*b[k*4+c];
            out[r*4+c]=int32_t(uint32_t(sum>>12));
        }
        return out;
    }
    Matrix &current() {return mode==0?projection:mode==3?texture:position;}
    static int arguments(unsigned cmd) {
        switch(cmd) {
        case 0:case 0x11:case 0x15:case 0x41:return 0;
        case 0x16:case 0x18:return 16;
        case 0x17:case 0x19:return 12;
        case 0x1a:return 9;
        case 0x1b:case 0x1c:case 0x70:return 3;
        case 0x23:case 0x71:return 2;
        case 0x34:return 32;
        case 0x10:case 0x12:case 0x13:case 0x14:
        case 0x20:case 0x21:case 0x22:case 0x24:case 0x25:case 0x26:case 0x27:case 0x28:
        case 0x29:case 0x2a:case 0x2b:case 0x30:case 0x31:case 0x32:case 0x33:
        case 0x40:case 0x50:case 0x60:case 0x72:return 1;
        default:return -1;
        }
    }
    static int32_t signed10(uint32_t n) {n&=1023;return n&512?int32_t(n)-1024:int32_t(n);}
    void transform_position(int32_t *result) const {
        auto clip=multiply(position,projection);
        for(unsigned c=0;c<4;++c) {
            __int128 sum=int64_t(clip[12+c])*4096;
            for(unsigned n=0;n<3;++n)sum+=int64_t(coordinate[n])*clip[n*4+c];
            result[c]=int32_t(uint32_t(sum>>12));
        }
    }
    bool box_test() {
        int32_t origin[3]={int16_t(parameters[0]),int16_t(parameters[0]>>16),int16_t(parameters[1])};
        int32_t size[3]={int16_t(parameters[1]>>16),int16_t(parameters[2]),int16_t(parameters[2]>>16)};
        for(unsigned c=0;c<3;++c)if(origin[c]+size[c]<-32768||origin[c]+size[c]>32767) {rejected_command=0x70;return false;}
        using Clip=std::array<double,4>;Clip corners[8];auto matrix=multiply(position,projection);
        for(unsigned n=0;n<8;++n)for(unsigned c=0;c<4;++c) {
            __int128 sum=int64_t(matrix[12+c])*4096;
            for(unsigned axis=0;axis<3;++axis)sum+=int64_t(origin[axis]+((n&(1u<<axis))?size[axis]:0))*matrix[axis*4+c];
            corners[n][c]=int32_t(uint32_t(sum>>12));
        }
        // Test the six FACES, not just whether the bounding volumes overlap.
        // GBATEK BOX_TEST explicitly excludes a box enclosing the whole frustum.
        static constexpr unsigned faces[6][4]={{0,2,6,4},{1,5,7,3},{0,4,5,1},{2,3,7,6},{0,1,3,2},{4,6,7,5}};
        box_result=false;
        for(const auto &face:faces) {
            std::array<Clip,16> current{},next{};unsigned count=4;bool beyond_far=false;
            for(unsigned n=0;n<4;++n) {current[n]=corners[face[n]];beyond_far|=current[n][2]>current[n][3];}
            if(beyond_far&&!(polygon_attributes&0x1000))continue;
            for(unsigned plane=0;plane<6&&count;++plane) {
                auto distance=[&](const Clip &v) {return v[3]+(plane&1?-v[plane/2]:v[plane/2]);};
                unsigned output=0;auto previous=current[count-1];double dp=distance(previous);
                for(unsigned n=0;n<count;++n) {
                    const auto &point=current[n];double dc=distance(point);
                    if((dc>=0)!=(dp>=0)) {
                        if(output>=next.size())return false;double t=dp/(dp-dc);
                        for(unsigned c=0;c<4;++c)next[output][c]=previous[c]+(point[c]-previous[c])*t;
                        ++output;
                    }
                    if(dc>=0) {if(output>=next.size())return false;next[output++]=point;}
                    previous=point;dp=dc;
                }
                current=next;count=output;
            }
            if(count<3)continue;
            bool valid=true;double xmin=1e30,ymin=1e30,xmax=-1e30,ymax=-1e30;uint32_t viewport=attributes[0x60];
            double width=double(int((viewport>>16)&255)-int(viewport&255)+1),height=double(int(viewport>>24)-int((viewport>>8)&255)+1);
            for(unsigned n=0;n<count;++n) {
                const auto &v=current[n];if(v[3]<=0) {valid=false;break;}
                double x=std::floor((v[0]/v[3]+1)*width/2),y=std::floor((v[1]/v[3]+1)*height/2);
                xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);
            }
            if(!valid)continue;
            if(xmin==xmax&&ymin==ymax&&!(polygon_attributes&0x2000)) {
                bool near=false;for(unsigned n=0;n<count;++n)near|=current[n][3]<=double(attributes[0x61]&0x7fff)*512;
                if(!near)continue;
            }
            box_result=true;break;
        }
        return true;
    }
    void transform_direction(const int32_t *input,int32_t *output) const {
        for(unsigned c=0;c<3;++c) {
            int64_t sum=0;for(unsigned n=0;n<3;++n)sum+=int64_t(input[n])*direction[n*4+c];
            output[c]=int32_t(uint32_t(sum>>12));
        }
    }
    void normal(uint32_t word) {
        int32_t input[3],normal[3];for(unsigned n=0;n<3;++n)input[n]=signed10(word>>(n*10));
        if(attributes[0x2a]>>30==2)transform_texture(2,input);
        transform_direction(input,normal);
        uint32_t diffuse_ambient=attributes[0x30],specular_emission=attributes[0x31];
        int64_t color[3];for(unsigned c=0;c<3;++c)color[c]=(specular_emission>>(16+5*c))&31;
        for(unsigned light=0;light<4;++light)if(polygon_attributes&(1u<<light)) {
            int64_t dot=0,half=0;
            for(unsigned n=0;n<3;++n) {dot-=int64_t(light_vector[light][n])*normal[n];half-=int64_t(half_vector[light][n])*normal[n];}
            int64_t level=std::clamp(dot>>9,int64_t(0),int64_t(512));
            int64_t shine=std::clamp(half>>9,int64_t(0),int64_t(512));shine=(shine*shine)>>9;
            if(specular_emission&0x8000) {
                unsigned index=unsigned(std::min(shine>>2,int64_t(127)));
                shine=((attributes[0xa0+index/4]>>((index%4)*8))&255)*2;
            }
            uint32_t light_color=attributes[0x90+light];
            for(unsigned c=0;c<3;++c) {
                int64_t l=(light_color>>(5*c))&31,d=(diffuse_ambient>>(5*c))&31;
                int64_t a=(diffuse_ambient>>(16+5*c))&31,s=(specular_emission>>(5*c))&31;
                color[c]+=((s*l*shine)>>14)+((d*l*level)>>14)+((a*l)>>5);
            }
        }
        current_color=0;for(unsigned c=0;c<3;++c)current_color|=uint32_t(std::clamp(color[c],int64_t(0),int64_t(31)))<<(5*c);
    }
    void transform_texture(unsigned source,const int32_t *xyz) {
        if(source==0) {std::copy(texcoord,texcoord+2,transformed_texcoord);return;}
        for(unsigned c=0;c<2;++c) {
            int64_t sum=0;
            if(source==1) {
                sum=int64_t(texcoord[0])*texture[c]+int64_t(texcoord[1])*texture[4+c]+texture[8+c]+texture[12+c];
                transformed_texcoord[c]=int16_t(sum>>12);
            } else {
                for(unsigned n=0;n<3;++n)sum+=int64_t(xyz[n])*texture[n*4+c];
                transformed_texcoord[c]=int16_t((sum>>(source==2?17:20))+texcoord[c]);
            }
        }
    }
    // Account for geometry AFTER clipping, as the hardware RAM counters do.
    // Rendering retains the original polygon for its separate attribute-aware
    // clipper; fully invisible geometry must not exhaust the scene's RAM.
    static unsigned ram_vertices(const GeometryPolygon &polygon,bool &clipped) {
        using Point=std::array<double,4>;std::array<Point,16> points{},next{};
        unsigned count=polygon.count;clipped=false;
        for(unsigned n=0;n<count;++n)for(unsigned c=0;c<4;++c)points[n][c]=polygon.vertex[n].clip[c];
        for(unsigned n=0;n<count;++n)if(points[n][2]>points[n][3]&&!(polygon.attributes&0x1000))return 0;
        for(unsigned plane=0;plane<6&&count;++plane) {
            auto distance=[&](const Point &v) {return v[3]+(plane&1?-v[plane/2]:v[plane/2]);};
            unsigned output=0;auto previous=points[count-1];double dp=distance(previous);
            for(unsigned n=0;n<count;++n) {
                auto current=points[n];double dc=distance(current);
                if((dc>=0)!=(dp>=0)) {
                    clipped=true;if(output>=next.size())return 0;double t=dp/(dp-dc);
                    for(unsigned c=0;c<4;++c)next[output][c]=previous[c]+(current[c]-previous[c])*t;++output;
                }
                if(dc>=0) {if(output>=next.size())return 0;next[output++]=current;}else clipped=true;
                previous=current;dp=dc;
            }
            points=next;count=output;
        }
        unsigned positive=0;for(unsigned n=0;n<count;++n)positive+=points[n][3]>0;
        return positive>=3?positive:0;
    }
    bool vertex() {
        if(!begun) {rejected_command=0x23;return false;}
        if((attributes[0x2a]>>30)==3)transform_texture(3,coordinate);
        GeometryVertex &v=pending[vertex_count++];transform_position(v.clip);
        v.st[0]=transformed_texcoord[0];v.st[1]=transformed_texcoord[1];v.rgb=current_color;
        ++vertices;++primitive_vertices;
        unsigned needed=(primitive==0||primitive==2)?3:4;
        if(vertex_count<needed)return true;
        GeometryPolygon polygon{};polygon.count=needed;polygon.attributes=polygon_attributes;
        polygon.texture=attributes[0x2a];polygon.palette=attributes[0x2b];polygon.viewport=attributes[0x60];
        for(unsigned n=0;n<needed;++n) {
            unsigned index=n;
            if(primitive==2&&(primitive_vertices&1)==0&&n<2)index=1-n;
            if(primitive==3&&n>=2)index=5-n;
            polygon.vertex[n]=pending[index];
        }
        bool clipped=false;unsigned stored=ram_vertices(polygon,clipped);
        unsigned cost=stored;
        if(stored&&!clipped&&strip_stored&&primitive>=2)cost-=2;
        if(stored) {
            if(building.size()>=2048||cost>6144-list_vertices) {rejected_command=0x23;return false;}
            building.push_back(polygon);list_vertices+=cost;
        }
        strip_stored=stored&&!clipped;
        if(primitive==2) {pending[0]=pending[1];pending[1]=pending[2];vertex_count=2;}
        else if(primitive==3) {pending[0]=pending[2];pending[1]=pending[3];vertex_count=2;}
        else vertex_count=0;
        return true;
    }
    bool execute(unsigned cmd) {
        if(arguments(cmd)<0)return true; // Invalid GX codes consume no parameters.
        if(cmd==0||cmd==0x41)return true;
        if(swap_pending) {
            unsigned words=1+unsigned(arguments(cmd));
            if(deferred_words+words>256) {rejected_command=cmd;return false;}
            Queued queued{};queued.command=cmd;std::copy(parameters,parameters+32,queued.words.begin());
            deferred.push_back(queued);deferred_words+=words;return true;
        }
        return execute_now(cmd);
    }
    void vblank(const HostVram &vram,uint16_t control) {
        if(!swap_pending)return;
        completed=std::move(building);building.clear();list_vertices=0;
        completed_parameters=swap_parameters;std::memcpy(completed_settings,settings,sizeof(settings));
        swap_parameters=next_swap_parameters;
        std::memcpy(completed_banks,vram.control,9);completed_vram=vram.bytes;completed_control=control;
        completed_dot_depth=uint16_t(attributes[0x61]);
        swap_pending=false;begun=false;strip_stored=false;vertex_count=primitive_vertices=0;++swaps;
        std::array<uint32_t,32> parser_parameters;std::copy(parameters,parameters+32,parser_parameters.begin());
        unsigned consumed=0;
        while(consumed<deferred.size()&&!swap_pending) {
            auto &item=deferred[consumed++];std::copy(item.words.begin(),item.words.end(),parameters);
            deferred_words-=1+unsigned(arguments(item.command));
            if(!execute_now(item.command)) {failed=true;break;}
        }
        deferred.erase(deferred.begin(),deferred.begin()+consumed);
        std::copy(parser_parameters.begin(),parser_parameters.end(),parameters);
    }
    bool execute_now(unsigned cmd) {
        if(cmd==0||cmd==0x41)return true;
        uint32_t p=parameters[0];
        if(cmd==0x70)return box_test();
        if(cmd==0x50) {
            bool incomplete=begun&&primitive_vertices&&((primitive<2&&vertex_count)||(primitive==2&&primitive_vertices<3)||
                (primitive==3&&(primitive_vertices<4||primitive_vertices&1)));
            if(incomplete) {rejected_command=cmd;return false;}
            // Swap parameters apply to the FOLLOWING list, matching the hardware.
            // completed_parameters is set from this prior list at VBlank below.
            empty_clear_color=0;
            for(unsigned n=0;n<4;++n)empty_clear_color|=uint32_t(settings[0x30+n])<<(n*8);
            if(building.empty())++empty_swaps;
            next_swap_parameters=p&3;swap_pending=true;return true;
        }
        if(cmd==0x40) {primitive=p&3;vertex_count=primitive_vertices=0;begun=true;strip_stored=false;polygon_attributes=attributes[0x29];return true;}
        if(cmd==0x20) {current_color=p&0x7fff;attributes[cmd]=p;return true;}
        if(cmd==0x21) {normal(p);return true;}
        if(cmd==0x22) {
            texcoord[0]=int16_t(p);texcoord[1]=int16_t(p>>16);attributes[cmd]=p;
            unsigned transform=attributes[0x2a]>>30;if(transform<=1)transform_texture(transform,nullptr);return true;
        }
        if(cmd>=0x23&&cmd<=0x28||cmd==0x71) {
            if(cmd==0x23||cmd==0x71) {coordinate[0]=int16_t(p);coordinate[1]=int16_t(p>>16);coordinate[2]=int16_t(parameters[1]);}
            if(cmd==0x24)for(unsigned n=0;n<3;++n)coordinate[n]=signed10(p>>(n*10))*64;
            if(cmd==0x25||cmd==0x26)coordinate[0]=int16_t(p);
            if(cmd==0x25)coordinate[1]=int16_t(p>>16);
            if(cmd==0x26)coordinate[2]=int16_t(p>>16);
            if(cmd==0x27) {coordinate[1]=int16_t(p);coordinate[2]=int16_t(p>>16);}
            if(cmd==0x28)for(unsigned n=0;n<3;++n) {
                int32_t sum=coordinate[n]+signed10(p>>(n*10));
                if(sum<-32768||sum>32767) {rejected_command=cmd;return false;}
                coordinate[n]=sum;
            }
            if(cmd==0x71) {transform_position(position_result);return true;}
            return vertex();
        }
        if(cmd==0x72) {
            int32_t input[3];for(unsigned n=0;n<3;++n)input[n]=signed10(p>>(n*10));
            for(unsigned c=0;c<3;++c) {
                int64_t sum=0;for(unsigned n=0;n<3;++n)sum+=int64_t(input[n])*direction[n*4+c];
                int32_t output=int32_t(sum>>9)&8191;vector_result[c]=output&4096?output-8192:output;
            }
            return true;
        }
        if(cmd==0x10) {mode=p&3;return true;}
        if(cmd>=0x11&&cmd<=0x14) {
            bool single=mode==0||mode==3;
            unsigned index=single?0:p&31;
            if(cmd==0x11) {
                index=single?0:position_level;
                if(single) {if(mode==0) {if(projection_level)error=true;projection_level=1;}}
                else {position_level=(position_level+1)&63;if(index>=31)error=true;}
            } else if(cmd==0x12) {
                if(single) {if(mode==0) {if(!projection_level)error=true;projection_level=0;}}
                else {
                    int delta=int(p&63);if(delta&32)delta-=64;
                    position_level=(position_level-delta)&63;index=position_level;
                    if(index>=31)error=true;
                }
            } else if(!single&&index==31)error=true;
            index&=31;
            bool store=cmd==0x11||cmd==0x13;
            if(mode==0) {if(store)projection_stack=projection;else projection=projection_stack;}
            else if(mode==3) {if(store)texture_stack=texture;else texture=texture_stack;}
            else if(store) {position_stack[index]=position;direction_stack[index]=direction;}
            else {position=position_stack[index];direction=direction_stack[index];}
            return true;
        }
        if(cmd>=0x15&&cmd<=0x1c) {
            Matrix m=identity();
            if(cmd==0x16||cmd==0x18)for(unsigned n=0;n<16;++n)m[n]=int32_t(parameters[n]);
            if(cmd==0x17||cmd==0x19)for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)m[r*4+c]=int32_t(parameters[r*3+c]);
            if(cmd==0x1a)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)m[r*4+c]=int32_t(parameters[r*3+c]);
            if(cmd==0x1b)for(unsigned n=0;n<3;++n)m[n*5]=int32_t(parameters[n]);
            if(cmd==0x1c)for(unsigned n=0;n<3;++n)m[12+n]=int32_t(parameters[n]);
            bool load=cmd<=0x17;
            current()=load?m:multiply(m,current());
            if(mode==2&&cmd!=0x1b)direction=load?m:multiply(m,direction);
            return true;
        }
        if(cmd==0x29||cmd==0x2a||cmd==0x2b||
           (cmd>=0x30&&cmd<=0x34)||cmd==0x60) {
            attributes[cmd]=p; // Full light and shininess data retained separately below.
            if(cmd==0x30&&(p&0x8000))current_color=p&0x7fff;
            if(cmd==0x32) {
                unsigned light=p>>30;int32_t input[3];for(unsigned n=0;n<3;++n)input[n]=signed10(p>>(n*10));
                transform_direction(input,light_vector[light]);
                for(unsigned n=0;n<3;++n)half_vector[light][n]=(light_vector[light][n]-(n==2?512:0))>>1;
            }
            if(cmd==0x33)attributes[0x90+(p>>30)]=p&0x7fff;
            if(cmd==0x34)for(unsigned n=0;n<32;++n)attributes[0xa0+n]=parameters[n];
            return true;
        }
        rejected_command=cmd;return false;
    }
    bool finish_zero() {
        while(next_command<command_count) {
            unsigned cmd=commands[next_command];int count=arguments(cmd);
            if(count<0) {++next_command;continue;}
            if(count)return true;
            if(!execute(cmd))return false;
            ++next_command;
        }
        command_count=next_command=parameter_count=0;return true;
    }
    bool feed(uint32_t word) {
        if(!command_count) {
            for(unsigned n=0;n<4;++n)commands[n]=uint8_t(word>>(8*n));
            next_command=parameter_count=0;command_count=4;
            return finish_zero();
        }
        unsigned cmd=commands[next_command];int count=arguments(cmd);
        if(count<=0||parameter_count>=32)return false;
        parameters[parameter_count++]=word;
        if(parameter_count==unsigned(count)) {
            if(!execute(cmd))return false;
            parameter_count=0;++next_command;return finish_zero();
        }
        return true;
    }
    uint32_t status() const {
        bool test_waiting=false;
        for(const auto &item:deferred)test_waiting|=item.command>=0x70&&item.command<=0x72;
        if(command_count&&next_command<command_count)test_waiting|=commands[next_command]>=0x70&&commands[next_command]<=0x72;
        return (test_waiting?1u:0u)|(box_result?2u:0u)|(position_level&31)<<8|(projection_level&1)<<13|(error?0x8000:0)|
            (deferred_words<<16)|(deferred_words<128?1u<<25:0)|(!deferred_words?1u<<26:0)|
            (swap_pending?1u<<27:0)|irq_mode<<30;
    }
    uint32_t next_swap_parameters=0;
    void irq(uint32_t &flags) const {if(irq_mode==1&&deferred_words<128||irq_mode==2&&!deferred_words)flags|=1u<<21;}
    bool access(uint32_t address,unsigned width,uint32_t &value,bool writing,uint32_t &flags) {
        if(!writing&&address>=0x04000600&&address<0x04000604&&(width==1||width==2||width==4)&&width<=0x04000604-address) {
            uint32_t mask=width==4?0xffffffffu:(1u<<(width*8))-1;value=(status()>>((address-0x04000600)*8))&mask;irq(flags);return true;
        }
        if(address==0x04000600&&width==4) {
            if(writing) {irq_mode=value>>30;if(irq_mode==3)return false;if(value&0x8000) {error=false;projection_level=0;}}
            else value=status();
            irq(flags);return true;
        }
        if(address==0x04000604&&width==4&&!writing) {value=uint32_t(building.size())|(list_vertices<<16);return true;}
        if(address>=0x04000620&&address<0x04000636&&!writing&&(width==2||width==4)&&width<=0x04000636-address) {
            value=0;
            for(unsigned n=0;n<width;++n) {
                unsigned index=address+n-0x04000620;
                if(index<16)value|=uint32_t(uint8_t(uint32_t(position_result[index/4])>>((index%4)*8)))<<(8*n);
                else value|=uint32_t(uint8_t(uint32_t(vector_result[(index-16)/2])>>((index%2)*8)))<<(8*n);
            }
            return true;
        }
        if(address>=0x04000640&&address<0x040006a4&&width==4&&!writing) {
            if(address<0x04000680) {auto clip=multiply(position,projection);value=uint32_t(clip[(address-0x04000640)/4]);}
            else {unsigned index=(address-0x04000680)/4;value=uint32_t(direction[(index/3)*4+index%3]);}
            return true;
        }
        if(address>=0x04000400&&address<0x04000600&&width==4&&writing&&(address&3)==0) {
            if(address<0x04000440)return feed(value);
            unsigned cmd=(address-0x04000400)/4;int count=arguments(cmd);
            if(count<0)return true;
            if(!command_count) {commands[0]=uint8_t(cmd);command_count=1;next_command=parameter_count=0;}
            else if(commands[next_command]!=cmd)return false;
            if(!count) {bool ok=execute(cmd);command_count=next_command=parameter_count=0;return ok;}
            return feed(value);
        }
        if(address>=0x04000440&&address<0x04000600&&width==4&&!writing&&(address&3)==0&&arguments((address-0x04000400)/4)>=0) {
            // These documented write-only command ports have no state readback.
            // Zero is the provisional unmapped-bus value; synchronization reads
            // must not replay commands or return our retained software state.
            value=0;return true;
        }
        if(address>=0x04000330&&address<0x040003c0&&writing&&width<=4&&width<=0x040003c0-address) {
            for(unsigned n=0;n<width;++n)settings[address-0x04000320+n]=uint8_t(value>>(8*n));
            return true;
        }
        if(address==0x04000610&&writing&&width==2) {attributes[0x61]=value&0x7fff;return true;}
        return false;
    }
};
