//
// Created by omarabdo on 6/19/26.
//

#ifndef AKATSUKIDB_CPP_TRANSACTIONS_HPP
#define AKATSUKIDB_CPP_TRANSACTIONS_HPP
#include "IStatement.hpp"

class BeginStatement    : public IStatement {};

class CommitStatement   : public IStatement {};

class RollbackStatement : public IStatement {};


#endif //AKATSUKIDB_CPP_TRANSACTIONS_HPP
