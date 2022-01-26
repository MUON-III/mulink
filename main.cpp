#include <iostream>
#include "cxxopts/cxxopts.hpp"
#include "mulink.h"

std::vector<std::string> split (const std::string& s, const std::string& delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    while ((pos_end = s.find (delimiter, pos_start)) != std::string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (s.substr (pos_start));
    return res;
}

int main(int argc, char** argv) {

    cxxopts::Options options("mulink", "Linker for MUON code");
    options.add_options()
            ("i,input","input files in format <binfile>;<mulinkfile>, use LINKONLY;<mulinkfile> to exclude that binary from the output",cxxopts::value<std::vector<std::string>>())
            ("o,output","output file, format binary", cxxopts::value<std::string>())
            ("org","set origin address", cxxopts::value<unsigned int>())
            ("l,library","create library (output mulink file for completed binary). parameter is section name.", cxxopts::value<std::string>())
            ("h,help","show this message")
    ;
    auto result = options.parse(argc, argv);

    if (result.count("help") || result.count("input") == 0 || result.count("output") != 1 || result.count("org") == 0) {
        std::cout << options.help() << std::endl;
        exit(0);
    }

    unsigned int org = result["org"].as<unsigned int>();

    std::vector<struct mulink_file_pair> files = std::vector<struct mulink_file_pair>();

    for (const auto& s : result["input"].as<std::vector<std::string>>()) {
        std::vector<std::string> tmp = split(s, ";");
        if (tmp.size() != 2) {
            printf("Invalid input argument: %s\n",s.c_str());
            exit(0);
        }

        std::string bin = tmp.at(0);
        std::string lnk = tmp.at(1);
        printf("Importing binary %s with linker file %s\n",bin.c_str(),lnk.c_str());

        bool linkonly = strcmp(bin.c_str(), "LINKONLY") == 0;
        FILE* binfp;
        if (!linkonly) {
            binfp = fopen(bin.c_str(), "rb");
            if (binfp == NULL) {
                perror("fopen(bin)");
                exit(0);
            }
        }
        FILE* lnkfp = fopen(lnk.c_str(), "r");
        if (lnkfp == NULL) {
            perror("fopen(lnk)");
            fclose(binfp);
            exit(0);
        }

        size_t binsz = 0;
        if (!linkonly) {
            fseek(binfp, 0, SEEK_END);
            binsz = ftell(binfp);
            fseek(binfp, 0, SEEK_SET);
        }

        if ((binsz%3) != 0) {
            printf("Error: file %s size not multiple of word size (size=%lu)!\n",bin.c_str(),binsz);
            fclose(binfp);
            fclose(lnkfp);
            exit(0);
        }

        struct mulink_file_pair fp{};
        if (!linkonly) {
            fp.bin = (unsigned char *) malloc(binsz);
        } else {
            fp.bin = NULL;
        }
        fp.binsz = binsz;
        fp.name = (char*)malloc(bin.length() + 1);
        strcpy(fp.name, bin.c_str());
        if (!linkonly) {
            if (fread(fp.bin, binsz, 1, binfp) != 1) {
                perror("fread(binfp)");
                fclose(binfp);
                fclose(lnkfp);
                exit(0);
            }
            printf("Read %lu bytes (%lu words) from %s\n", binsz, binsz / 3, bin.c_str());
        }

        if (!linkonly)
            fclose(binfp);

        fseek(lnkfp, 0, SEEK_END);
        size_t lnksz = ftell(lnkfp);
        fseek(lnkfp, 0, SEEK_SET);

        char* lnkb = (char*)malloc(lnksz);

        if (fread(lnkb, lnksz, 1, lnkfp) != 1) {
            perror("fread(lnkfp)");
            fclose(lnkfp);
            exit(0);
        }

        std::vector<std::string> lines = split(std::string(lnkb),"\n");

        if (strcmp(lines.at(0).c_str(), "!MULINK1") != 0) {
            printf("Error: file %s isn't a mulink file!\n", lnk.c_str());
            fclose(lnkfp);
            exit(0);
        }
        fclose(lnkfp);

        std::map<std::string, std::vector<struct mulink_function_def>> exports = std::map<std::string, std::vector<struct mulink_function_def>>();
        std::map<std::string, std::vector<struct mulink_lookup_function_def>> imports = std::map<std::string, std::vector<struct mulink_lookup_function_def>>();

        struct mulink_link_file lnkf{};
        for (const auto& line : lines) {
            if (strncmp(line.c_str(), "$ORG:", 5) == 0) { // import
                sscanf(line.c_str()+5, "%X", &lnkf.origin);
                printf("[%s] origin: 0x%06X\n",lnk.c_str(),lnkf.origin);
            } else if (strncmp(line.c_str(), "$SEC:", 5) == 0 && line.length() > 5) { // section
                lnkf.section = (char*)malloc(line.length() - 5);
                strcpy(lnkf.section,line.c_str()+5);
                printf("[%s] section: %s\n",lnk.c_str(),lnkf.section);
            } else if (line.length() > 1 && line.c_str()[0] == '+') { // export
                std::vector<std::string> fields = split(line, ";");
                if (fields.size() == 2) {
                    unsigned int ptr = strtol(fields.at(1).c_str(),NULL,16);
                    char* name = (char*)malloc(fields.at(0).length());
                    strcpy(name, fields.at(0).c_str()+1);
                    struct mulink_function_def mfd{};
                    mfd.section = lnkf.section;
                    mfd.name = name;
                    if (lnkf.origin > ptr) {
                        printf("Relocation error! Origin is higher than function pointer! [fname=%s]\n",mfd.name);
                        exit(0);
                    }
                    mfd.offset = ptr - lnkf.origin;
                    if (exports.count(std::string(lnkf.section)) == 0)
                        exports[lnkf.section] = std::vector<struct mulink_function_def>();
                    exports.at(std::string(lnkf.section)).push_back(mfd);
		    printf("[%s] export function: %s at 0x%06X\n",lnk.c_str(),mfd.name,mfd.offset);
                }
            } else if (line.length() > 1 && line.c_str()[0] == '-' && !linkonly) { // import
                std::vector<std::string> fields = split(line, ";");
                if (fields.size() == 3) {
                    unsigned int ptr = strtol(fields.at(1).c_str(),NULL,16);
                    unsigned int mask = strtol(fields.at(2).c_str(),NULL,16);
                    char* name = (char*)malloc(fields.at(0).length());
                    strcpy(name, fields.at(0).c_str()+1);
                    struct mulink_lookup_function_def mlfd;
                    mlfd.section = lnkf.section;
                    mlfd.name = name;
                    if (lnkf.origin > ptr) {
                        printf("Lookup error! Origin is higher than function pointer! [fname=%s]\n",mlfd.name);
                        exit(0);
                    }
                    mlfd.lookupoffset = ptr - lnkf.origin;
                    mlfd.lookupmask = mask;
                    if (imports.count(std::string(lnkf.section)) == 0)
                        imports[lnkf.section] = std::vector<struct mulink_lookup_function_def>();
                    imports.at(std::string(lnkf.section)).push_back(mlfd);
                    printf("[%s] import function: %s at 0x%06X:0x%06X\n",lnk.c_str(),mlfd.name,mlfd.lookupoffset,mlfd.lookupmask);
                }
            }
        }

        lnkf.hassz = exports.size();
        lnkf.wantssz = imports.size();

        lnkf.has = (struct mulink_function_def**)malloc(sizeof(struct mulink_function_def*) * lnkf.hassz);
        lnkf.wants = (struct mulink_lookup_function_def**)malloc(sizeof(struct mulink_lookup_function_def*) * lnkf.wantssz);

        for (const auto& i : exports) {
            int f = 0;
            for (auto e : i.second) {
                lnkf.has[f] = (struct mulink_function_def*)malloc(sizeof(struct mulink_function_def));
                memcpy(lnkf.has[f], &e, sizeof(mulink_function_def));
                f++;

                printf("[%s] export %s at 0x%06zX in section %s\n",lnk.c_str(),e.name,(size_t)e.offset,i.first.c_str());
            }
        }

        for (const auto& i : imports) {
            int f = 0;
            for (auto e : i.second) {
                lnkf.wants[f] = (struct mulink_lookup_function_def*)malloc(sizeof(struct mulink_lookup_function_def));
                memcpy(lnkf.wants[f], &e, sizeof(mulink_lookup_function_def));
                f++;

                printf("[%s] import %s at 0x%06X:0x%06X in section %s\n",lnk.c_str(),e.name,e.lookupoffset,e.lookupmask,i.first.c_str());
            }
        }

        fp.lnk = lnkf;
        files.push_back(fp);
    }

    printf("Doing relocation....\n");

    size_t arrsz = 0;
    for (auto f : files)
        arrsz += f.binsz/3;

    auto* out = (unsigned int*)malloc((arrsz + org) * sizeof(unsigned int));

    size_t ptr = org;
    for (auto f : files) {
        auto *tmp = (unsigned int*)malloc((f.binsz/3) * sizeof(unsigned int));

        for (int i=0;i<f.binsz/3;i++) {
            tmp[i] = 0;
            tmp[i] |= (f.bin[(i*3)] << 16);
            tmp[i] |= (f.bin[(i*3)+1] << 8);
            tmp[i] |= (f.bin[(i*3)+2]);
        }

        memcpy(out+ptr, tmp, sizeof(unsigned int) * (f.binsz/3));
        free(tmp);

        f.lnk.origin = ptr;
        for (int i=0;i<f.lnk.wantssz;i++)
            f.lnk.wants[i]->lookupoffset += ptr;

        for (int i=0;i<f.lnk.hassz;i++) {
            f.lnk.has[i]->offset += ptr;
            printf("[%s] label %s now at 0x%06X\n",f.name, f.lnk.has[i]->name, f.lnk.has[i]->offset);
        }

        printf("[%s] now at 0x%06zX\n",f.name, ptr);

        ptr += f.binsz/3;
    }

    std::map<std::string, std::vector<struct mulink_function_def*>> exports = std::map<std::string, std::vector<struct mulink_function_def*>>();
    std::map<std::string, std::vector<struct mulink_lookup_function_def*>> imports = std::map<std::string, std::vector<struct mulink_lookup_function_def*>>();

    for (auto f : files) {
        for (int i=0;i<f.lnk.hassz;i++) {
            if (exports.count(f.lnk.has[i]->section) == 0)
                exports[f.lnk.has[i]->section] = std::vector<struct mulink_function_def*>();
            exports[f.lnk.has[i]->section].push_back(f.lnk.has[i]);
        }

        for (int i=0;i<f.lnk.wantssz;i++) {
            if (imports.count(f.lnk.wants[i]->section) == 0)
                imports[f.lnk.wants[i]->section] = std::vector<struct mulink_lookup_function_def*>();
            imports[f.lnk.wants[i]->section].push_back(f.lnk.wants[i]);
        }

        if (exports.count(f.lnk.section) == 0)
            exports[f.lnk.section] = std::vector<struct mulink_function_def*>();
        if (imports.count(f.lnk.section) == 0)
            imports[f.lnk.section] = std::vector<struct mulink_lookup_function_def*>();
    }

    printf("Linking....\n");

    for (const auto& sec : imports) {
        for (auto f : sec.second) {
            if (f->resolved) continue;

            printf("[sec:%s] Resolving %s\n",sec.first.c_str(),f->name);

            if (strncmp(f->name, "%EXT:", 5) == 0 && strlen(f->name) > 5) { // allow imports from other sections
                for (const auto& sec2 : exports) {
                    for (auto f2 : sec2.second) {
                        if (strcmp(f2->name, f->name+5) == 0 || (strlen(f2->name)>5 && strcmp(f2->name+5, f->name+5)==0 && strncmp(f2->name, "%EXP:", 5)==0)) {
                            printf("[sec:%s] Label %s at 0x%06X in section %s\n",sec.first.c_str(),f->name+5,f2->offset,sec2.first.c_str());
                            out[f->lookupoffset] &= ~(f->lookupmask);
                            out[f->lookupoffset] = out[f->lookupoffset] | (f2->offset&f->lookupmask);
                            f->resolved = true;
                        }
                    }
                }
            } else {
                for (auto f2 : exports[sec.first]) {
                    if (strcmp(f2->name, f->name) == 0 || (strlen(f2->name)>5 && strcmp(f2->name+5, f->name)==0 && strncmp(f2->name, "%EXP:", 5)==0)) {
                        printf("[sec:%s] Label %s at 0x%06X\n",sec.first.c_str(),f->name,f2->offset);
                        out[f->lookupoffset] &= ~(f->lookupmask);
                        out[f->lookupoffset] = out[f->lookupoffset] | (f2->offset&f->lookupmask);
                        f->resolved = true;
                    }
                }
            }

            if (!f->resolved) {
                printf("[sec:%s] Error: unable to resolve label %s!\n",sec.first.c_str(),f->name);
                exit(0);
            }
        }
    }

    std::string oname = result["output"].as<std::string>();

    if (result.count("library")) {
        printf("Generating mulink file...\n");

        char* on = (char*)malloc(oname.length() + 7);
        sprintf(on, "%s.mulink",oname.c_str());

        char* fbuf = (char*)malloc(1024);

        FILE* fp = fopen(on, "w");
        free(on);

        snprintf(fbuf, 1023, "!MULINK1\n$SEC:%s\n$ORG:%06X\n",result["library"].as<std::string>().c_str(), org);
        fwrite(fbuf, strlen(fbuf), 1, fp);

        for (const auto& sec : exports) {
            for (auto f: sec.second) {
                if (strlen(f->name) > 5 && strncmp(f->name, "%EXP:", 5) == 0) {
                    printf("Adding function %s [at 0x%06X] to exports..\n",f->name + 5, f->offset);
                    snprintf(fbuf, 1023, "+%s+%06X\n",f->name + 5, f->offset);
                    fwrite(fbuf, strlen(fbuf), 1, fp);
                }
            }
        }

        fclose(fp);
        printf("Written MULINK file\n");
        free(fbuf);
    }

    printf("Generating output file..\n");

    FILE* fp = fopen(oname.c_str(), "wb");
    unsigned char tmp[3];
    for (unsigned int i=org;i<arrsz+org;i++) {
        tmp[0] = (out[i]&0xFF0000)>>16;
        tmp[1] = (out[i]&0xFF00)>>8;
        tmp[2] = (out[i]&0xFF);
        fwrite(tmp, 3, 1, fp);
    }
    fclose(fp);

    printf("Freeing memory..");

    int freed = 0;
    for (auto f : files) {
        for (int i = 0; i < f.lnk.hassz; i++) {
            free(f.lnk.has[i]->name);
            free(f.lnk.has[i]);
            freed+=2;
        }

        for (int i = 0; i < f.lnk.wantssz; i++) {
            free(f.lnk.wants[i]->name);
            free(f.lnk.wants[i]);
            freed+=2;
        }

        free(f.lnk.has);
        free(f.lnk.wants);
        free(f.lnk.section);
        free(f.bin);
        free(f.name);
        freed+=5;
    }
    printf("freed %d objects\n",freed);

    printf("mulink done.\n");

    return 0;
}
