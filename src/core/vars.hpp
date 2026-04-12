#pragma once
#include <string>
#include <vector>

bool     tryAssign(const std::string& text);
size_t   countLeadingAssignments(const std::vector<std::string>& args);
void     applyInlineEnvChild(const std::vector<std::string>& args, size_t count);
void     loadPersistVars();
void     savePersistVars();
