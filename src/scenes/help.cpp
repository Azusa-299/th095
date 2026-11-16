#include "help.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include "../log.hpp"

namespace th95::scene {
void HelpScene::close()
{
    // The worker reads Archive only, never the D3D device or scene.
    if(m_pageRead.valid())m_pageRead.wait();
    m_pageRead={};m_pageData.clear();
    if(m_title)m_title->bindExternalPage(13,nullptr);
    m_title=nullptr;m_stage=Stage::Closed;
    m_rows={};m_panels={};m_heading={};m_pageAnimation={};
    m_outgoingPages.clear();m_page=format::AnmTexture{};
}

bool HelpScene::open(Archive &archive,IDirect3DDevice9 *device,format::AnmTexture &title,
                     platform::Audio &audio)
{
    close();
    m_archive=&archive;m_device=device;m_title=&title;m_audio=&audio;
    m_selected=0;m_stateTicks=0;m_failed=false;
    if(!title.bindExternalPage(13,&m_page)||!m_panels[0].start(title,104)||
       !m_panels[1].start(title,105,512)||!m_heading.start(title,24)) {
        close();return false;
    }
    // retail routine copies the first panel's position +512 to the second half.
    // State 0 clears the dynamic slot; it stays empty until the first upload.
    showList();m_stage=Stage::List;
    log::print("info : help mode 9: list up/down; page left/right; script 144 dynamic slot 13\r\n");
    return true;
}

void HelpScene::highlightSelection()
{
    for(int i=0;i<kPageCount;++i)m_rows[i].interrupt(i==m_selected?2:3);
}

void HelpScene::showList()
{
    for(int i=0;i<kPageCount;++i) {
        m_rows[i]={};
        if(!m_rows[i].start(*m_title,145+i))m_failed=true;
    }
    highlightSelection();
}

void HelpScene::retirePage()
{
    if(m_pageAnimation.alive()) {
        m_pageAnimation.interrupt(1);
        m_outgoingPages.push_back(std::move(m_pageAnimation));
        m_pageAnimation={};
    }
}

void HelpScene::beginPageLoad()
{
    char name[32];std::snprintf(name,sizeof(name),"help_%02d.anm",m_selected);
    m_pageName=name;
    const Archive *archive=m_archive;
    const Entry *entry=archive->find(m_pageName);
    // retail routine reads/decrypts off-thread. State 3 uploads on the main thread.
    // Keep the old slot and fading page alive during the read.
    m_pageRead=std::async(std::launch::async,[archive,entry] {
        if(!entry)return std::vector<uint8_t>{};
        auto data=archive->extract(*entry);
        return data.size()==entry->size?data:std::vector<uint8_t>{};
    });
    for(auto &row:m_rows)row.interrupt(1);
    m_audio->playConfirm(); // se id 10, after id 41 on a page turn.
    m_stage=Stage::Loading;m_stateTicks=0;
}

bool HelpScene::uploadPage()
{
    format::AnmTexture loaded;
    if(!loaded.loadData(m_pageData,m_pageName,m_device)||loaded.pageCount()!=1||
       loaded.pageWidth(0)!=512||loaded.pageHeight(0)!=512) {
        log::print("error : help upload failed: %s\r\n",m_pageName.c_str());
        m_pageData.clear();m_failed=true;
        // Port-specific failure recovery returns to a usable list.
        showList();m_stage=Stage::List;m_stateTicks=0;return false;
    }
    m_page=std::move(loaded);m_pageData.clear();
    if(!m_pageAnimation.start(*m_title,144))m_failed=true;
    // The original starts the timer in state 2, not at upload completion.
    m_stage=Stage::Page;
    log::print("info : help page %d uploaded; sprite 128 at x=448->320 y=64\r\n",m_selected);
    return true;
}

void HelpScene::updateAnimations()
{
    for(auto &panel:m_panels)panel.update();
    for(auto &row:m_rows)row.update();
    m_heading.update();m_pageAnimation.update();
    for(auto &page:m_outgoingPages)page.update();
    m_outgoingPages.erase(std::remove_if(m_outgoingPages.begin(),m_outgoingPages.end(),
        [](const auto &a){return !a.alive();}),m_outgoingPages.end());
}

bool HelpScene::update(const HelpInput &input)
{
    if(m_stage==Stage::Closed)return false;
    bool returned=false;
    if(m_stage==Stage::Loading&&m_pageRead.valid()&&
       m_pageRead.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        m_pageData=m_pageRead.get();m_stage=Stage::Upload;
    }
    if(m_stage==Stage::Upload)uploadPage();
    if(m_stateTicks>=kInputDelay) {
        if(m_stage==Stage::List) {
            const int before=m_selected;
            // Independent branches preserve simultaneous-input behavior.
            if(input.up||input.upRepeat)m_selected=(m_selected+kPageCount-1)%kPageCount;
            if(input.down||input.downRepeat)m_selected=(m_selected+1)%kPageCount;
            if(before!=m_selected){highlightSelection();m_audio->playSelect();}
            if(input.confirm)beginPageLoad();
            else if(input.cancel) {
                m_audio->playCancel();
                for(auto &panel:m_panels)panel.interrupt(1);
                for(auto &row:m_rows)row.interrupt(1);
                m_heading.interrupt(1);
                m_stage=Stage::Closing;m_stateTicks=0;returned=true;
            }
        } else if(m_stage==Stage::Page) {
            int next=m_selected;
            // Right has priority except at the last page. No page key repeat.
            if(input.right&&m_selected<kPageCount-1)++next;
            else if(input.left&&m_selected>0)--next;
            if(next!=m_selected) {
                m_selected=next;m_audio->playShutter();retirePage();beginPageLoad();
            } else if(input.confirm||input.cancel) {
                m_audio->playCancel();retirePage();showList();
                m_stage=Stage::List;m_stateTicks=0;
                // Panels and heading persist; retain the page slot during its
                // outgoing 20-tick fade, without recreating the background.
            }
        }
    }
    ++m_stateTicks;updateAnimations();
    if(m_stage==Stage::Closing&&m_stateTicks>=60)close();
    return returned;
}

void HelpScene::renderLayer(IDirect3DDevice9 *device,int layer) const
{
    for(const auto &panel:m_panels)if(panel.layer()==layer)panel.draw(device);
    if(m_heading.layer()==layer)m_heading.draw(device);
    for(const auto &page:m_outgoingPages)if(page.layer()==layer)page.draw(device);
    if(m_pageAnimation.layer()==layer)m_pageAnimation.draw(device);
    for(const auto &row:m_rows)if(row.layer()==layer)row.draw(device);
}
} // namespace th95::scene
