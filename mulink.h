//
// Created by charlie on 25/01/2022.
//

#ifndef MULINK_MULINK_H
#define MULINK_MULINK_H

struct mulink_function_def {
    unsigned int offset;
    char* section;
    char* name;
};

struct mulink_lookup_function_def {
    unsigned int lookupoffset;
    unsigned int lookupmask;
    char* section;
    char* name;
    bool resolved = false;
};

struct mulink_link_file {
    struct mulink_function_def **has;
    struct mulink_lookup_function_def **wants;
    size_t hassz;
    size_t wantssz;
    unsigned int origin;
    char* section;
};

struct mulink_file_pair {
    unsigned char* bin;
    size_t binsz;
    struct mulink_link_file lnk;
    char* name;
};


#endif //MULINK_MULINK_H
