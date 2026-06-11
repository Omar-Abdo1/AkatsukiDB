//
// Created by omarabdo on 6/7/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_PAGEMANGER_HPP
#define AKATSUKIDB_CPP_PAGEMANGER_HPP
#include <fstream>
#include <memory>
#include <string>

#include "Page.hpp"


class PageManager {

    std::fstream _stream;
    std::string _filepath;
    int _totalPages;
   void InitializeNewFile();

public:

    PageManager(const std::string &filepath);

    ~PageManager(); // to destroy the object

    int AllocatePage();
    int TotalPages() const;

    std::unique_ptr<Page> ReadPage(int pageId);

    void WritePage(Page& page);
};


#endif //AKATSUKIDB_CPP_PAGEMANGER_HPP
