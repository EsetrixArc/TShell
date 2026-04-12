#pragma once
#include <string>

char*  completionGenerator(const char* text, int state);
char** tshCompletion(const char* text, int start, int end);
void   updateSuggestion(const std::string& current);
int    acceptSuggestion(int count, int key);
void   setupReadline();
