#include "music_cmt.hpp"

namespace th95::format {
bool MusicComments::parse(const std::vector<uint8_t> &data)
{
    m_tracks.clear();
    // retail address terminates a line at the first CR/LF and consumes every
    // following CR/LF, so blank separator lines are not separate records.
    std::vector<std::string> lines;
    std::size_t cursor=0;
    while(cursor<data.size()) {
        std::size_t end=cursor;
        while(end<data.size()&&data[end]!='\n'&&data[end]!='\r')++end;
        lines.emplace_back(reinterpret_cast<const char *>(data.data()+cursor),end-cursor);
        while(end<data.size()&&(data[end]=='\n'||data[end]=='\r'))++end;
        cursor=end;
    }
    std::size_t index=0;
    while(index<lines.size()) {
        const std::string &line=lines[index];
        if(line.empty()||line[0]=='#') {++index;continue;}
        if(line[0]!='@'||index+9>=lines.size()) {++index;continue;}
        MusicTrackComment track;
        track.bgm=line.substr(1);
        track.title=lines[index+1];
        for(int row=0;row<8;++row)track.comments[row]=lines[index+2+row];
        m_tracks.push_back(std::move(track));
        index+=10;
    }
    return !m_tracks.empty();
}
} // namespace th95::format
