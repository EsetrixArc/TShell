#pragma once
#include "globals.hpp"

void   printJob(const Job& j);
Job*   findJob(int id);
void   reapJobs();
int    waitFg(Job& j);
void   applyRedirects(const std::vector<Redirect>& reds);
