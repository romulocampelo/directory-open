#pragma once
#include "filesearch.h"
struct PreparedSearch;
SearchSummary searchEverything(const SearchRequest &, const PreparedSearch &, const std::atomic_bool &, const SearchUpdate &);
