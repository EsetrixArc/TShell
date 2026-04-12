#pragma once
#include "globals.hpp"
#include <istream>

std::vector<Section>    parseSections(const std::string& input);
std::vector<PipeStage>  parsePipes(const std::string& input);
std::string             extractRedirects(const std::string& raw,
                                         std::vector<Redirect>& reds,
                                         std::istream* scriptStream);
bool stripBackground(std::string& cmd);
bool isGroup(const std::string& t, bool& isSubshell);
