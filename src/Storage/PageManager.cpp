//
// Created by omarabdo on 6/7/26.
//

#include "../../include/AkatsukiDB/Storage/PageManager.hpp"


PageManager::PageManager(const std::string &filepath):_filepath(filepath) {

    //try to open binary file in Read/Writ mode
    _stream.open(_filepath , std::ios::in | std::ios::out | std::ios::binary);
    //The get cursor (g): Used for reading , The put cursor (p): Used for writing.

    if (!_stream.is_open()) {
        // Create the file explicitly
        _stream.open(_filepath, std::ios::out | std::ios::binary);
        _stream.close();
        // Reopen in standard Read/Write mode
        _stream.open(_filepath, std::ios::in | std::ios::out | std::ios::binary);
    }

    _stream.seekg (0, std::ios::end);
    std::streampos length = _stream.tellg(); // file size in bytes
    _totalPages = static_cast<int> (length)/PAGE_SIZE;

    if (_totalPages==0)
        InitializeNewFile();
}


PageManager::~PageManager() {
    if (_stream.is_open())
        _stream.close();
}

void PageManager::InitializeNewFile() {

    std::vector<uint8_t>headerPage(PAGE_SIZE,0);
    headerPage[0] = 'A';
    headerPage[1] = 'K';
    headerPage[2] = 'T';
    headerPage[3] = 'S';

    _stream.seekp(0, std::ios::beg); // Move write cursor to beginning
    _stream.write(reinterpret_cast<char*>(headerPage.data()), PAGE_SIZE);
    // stream only write char , so we convert the current data to char then write
    _stream.flush();

    _totalPages = 1;
}




int PageManager::AllocatePage() {

    int newPageid = _totalPages;
    std::vector<uint8_t>emptyData(PAGE_SIZE,0);
    _stream.seekp(0, std::ios::end); // Move write cursor to end
    _stream.write(reinterpret_cast<char*>(emptyData.data()), PAGE_SIZE);
    _stream.flush();

    ++_totalPages;
    return newPageid;
}

int PageManager::TotalPages() const {
 return _totalPages;
}

std::unique_ptr<Page> PageManager::ReadPage(int pageId) {
    if (pageId < 0 || pageId >= _totalPages) {
        throw std::out_of_range("Page " + std::to_string(pageId) + " does not exist in this file.");
    }

    std::vector<uint8_t> data(PAGE_SIZE);

    // Seek to the exact byte offset of the page
    _stream.seekg(static_cast<std::streampos>(pageId) * PAGE_SIZE, std::ios::beg);
    _stream.read(reinterpret_cast<char*>(data.data()), PAGE_SIZE);

    return std::make_unique<Page>(pageId, std::move(data));
    /*
     once PageManger creates the page with uniquePtr it transfere the Ownership to
     BufferPool then PageManger removes the local unique_ptr
     then BufferPool will promote it to shared_ptr so more than instance can use it
     */
}

void PageManager::WritePage(Page &page) {
  if (!page.isDirty())
      return;
    _stream.seekp(static_cast<std::streampos>(page.GetPageId()) * PAGE_SIZE, std::ios::beg);
    _stream.write(reinterpret_cast<const char*>(page.Data().data()), PAGE_SIZE);
    _stream.flush();

    page.ClearDirty();
}
