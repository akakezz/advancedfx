#pragma once
#ifndef _WIN32
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <cstring>
#include <cstdlib>
#include <strings.h>
#include <filesystem>
#define _stricmp strcasecmp
#define _wcsicmp wcscasecmp
#define _TRUNCATE size_t(-1)
#define __declspec(attribute)
inline int _wfopen_s(FILE **f,const wchar_t *p,const wchar_t *m) { std::string mode;for(;*m;++m)mode+=char(*m);*f=fopen(std::filesystem::path(p).c_str(),mode.c_str());return *f?0:1; }
template<size_t N> int _snprintf_s(char (&b)[N],size_t,const char *format,...) { va_list v;va_start(v,format);int r=vsnprintf(b,N,format,v);va_end(v);return r; }
#endif

#ifndef _WIN32
#include "../../deps/release/rapidxml/rapidxml.hpp"
namespace rapidxml { namespace internal {
template<class O,class C> O print_children(O,const xml_node<C>*,int,int);
template<class O,class C> O print_element_node(O,const xml_node<C>*,int,int);
template<class O,class C> O print_data_node(O,const xml_node<C>*,int,int);
template<class O,class C> O print_cdata_node(O,const xml_node<C>*,int,int);
template<class O,class C> O print_declaration_node(O,const xml_node<C>*,int,int);
template<class O,class C> O print_comment_node(O,const xml_node<C>*,int,int);
template<class O,class C> O print_doctype_node(O,const xml_node<C>*,int,int);
template<class O,class C> O print_pi_node(O,const xml_node<C>*,int,int);
}}
#endif
