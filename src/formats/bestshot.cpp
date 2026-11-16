#include "bestshot.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include "../lzss.hpp"

namespace th95::format {
namespace {
uint16_t u16(const uint8_t *p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
void put16(uint8_t *p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
void put32(uint8_t *p,uint32_t v){put16(p,uint16_t(v));put16(p+2,uint16_t(v>>16));}
// retail routine rounds each segment down to whole dwords.
uint32_t chunkSum(const uint8_t *data,std::size_t size)
{
    uint32_t sum=0;
    for(std::size_t i=0;i+4<=size;i+=4)sum+=uint32_t(data[i])|uint32_t(data[i+1])<<8|
                                           uint32_t(data[i+2])<<16|uint32_t(data[i+3])<<24;
    return sum;
}
}
std::string bestshotFileName(int mission)
{
    const int group=mission/10,scene=mission%10;
    char name[40];
    if(group==10)std::snprintf(name,sizeof(name),"bs_ex_%d.dat",scene+1);
    else std::snprintf(name,sizeof(name),"bs_%02d_%d.dat",group+1,scene+1);
    return name;
}
uint32_t bestshotChecksum(const uint8_t *header24,const uint8_t *metadata80,
                          const std::vector<uint8_t> &pixels)
{
    return chunkSum(header24,24)+chunkSum(metadata80,80)+chunkSum(pixels.data(),pixels.size());
}
std::vector<uint8_t> buildBestshotFile(int mission,int width,int height,uint32_t score,
                                       float rate,const std::vector<uint8_t> &pixels)
{
    if(width<=0||height<=0||width>0xFFFF||height>0xFFFF||
       pixels.size()!=std::size_t(width)*std::size_t(height)*3)return {};
    const int group=mission/10,scene=mission%10;
    std::array<uint8_t,24> header{};
    std::memcpy(header.data(),"BSTS",4);
    header[4]=2;           // retail routine stores mode 2 for a live capture.
    header[5]=3;           // source surface is 32-bit; the payload stays BGR.
    put16(header.data()+6,uint16_t(group+1));
    put16(header.data()+8,uint16_t(scene+1));
    put16(header.data()+10,0x0102);
    put16(header.data()+12,uint16_t(width));
    put16(header.data()+14,uint16_t(height));
    put32(header.data()+16,score);
    std::memcpy(header.data()+20,&rate,4);
    const auto packed=lzss::encode(pixels);
    std::vector<uint8_t> out(header.begin(),header.end());
    out.resize(24+80,0);
    out.insert(out.end(),packed.begin(),packed.end());
    return out;
}
bool Bestshot::load(const std::filesystem::path &path)
{
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file){*this={};return false;}
    const auto size=file.tellg();
    if(size<24||size>8*1024*1024){*this={};return false;}
    std::vector<uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0);file.read(reinterpret_cast<char *>(data.data()),data.size());
    if(!file){*this={};return false;}
    return parse(data);
}
bool Bestshot::parse(const std::vector<uint8_t> &bytes)
{
    *this={};
    if(bytes.size()<24||std::memcmp(bytes.data(),"BSTS",4))return false;
    const unsigned mode=bytes[4],depth=bytes[5];
    const int width=u16(bytes.data()+12),height=u16(bytes.data()+14);
    // retail routine uploads a 256x192 slot; a longer crop would read past the page.
    if(width<=0||height<=0||width>256||height>192||depth<1||depth>4)return false;
    const std::size_t payload=24+(mode==1?0:80);
    if(payload>=bytes.size())return false;
    const std::vector<uint8_t> compressed(bytes.begin()+std::ptrdiff_t(payload),bytes.end());
    const std::size_t expected=std::size_t(width)*std::size_t(height)*depth;
    const auto source=lzss::decode(compressed,expected);
    if(source.size()!=expected)return false;
    std::vector<uint8_t> page(256*256*4,0);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const std::size_t from=(std::size_t(y)*std::size_t(width)+std::size_t(x))*depth;
        const std::size_t to=(std::size_t(y)*256+std::size_t(x))*4;
        if(depth>=3) {
            // The stored triples are blue first; the page is BGRA.
            page[to]=source[from];page[to+1]=source[from+1];page[to+2]=source[from+2];
        } else {
            page[to]=page[to+1]=page[to+2]=source[from];
        }
        // retail routine uploads bpp 3 through D3DFMT 26 (A4R4G4B4) and marks the
        // alpha nibble with |= 0xF0, i.e. 15/15 fully opaque.  On the port's
        // A8R8G8B8 page that is 0xFF; 0xF0 would dim the photo by 6%.
        page[to+3]=0xFF;
    }
    // Mode 1 has no stored metadata, but retail routine still hashes 80 zero bytes.
    std::array<uint8_t,80> metadata{};
    if(mode!=1)std::memcpy(metadata.data(),bytes.data()+24,80);
    m_checksum=bestshotChecksum(bytes.data(),metadata.data(),source);
    m_width=width;m_height=height;m_pixels=std::move(page);
    return true;
}
} // namespace th95::format
