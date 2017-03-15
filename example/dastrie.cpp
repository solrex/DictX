#include <iostream>
#include <fstream>
#include <sys/time.h>
#include <unistd.h>
#include <cstdlib>

#include "dastrie.h"

typedef dastrie::builder<char *, uint32_t> builder_type;
typedef dastrie::trie<uint32_t> trie_type;
typedef builder_type::record_type record_type;

static bool record_cmp(const record_type &r1, const record_type &r2)
{
    return std::strcmp(r1.key, r2.key) < 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " File" << std::endl;
        return 1;
    }
    std::string filename = argv[1];
    std::string dbname = "";

    struct timeval begin, end;

    dastrie::trie<uint32_t> the_trie;

    if (filename.find(".db") == std::string::npos) {
        if (argc == 3) {
            dbname = argv[2];
        }
        gettimeofday(&begin, NULL);
        // 计算输入文件大小，并据此申请字符串存储的内存
        std::ifstream is(filename.c_str(), std::ios::binary | std::ios::ate);
        std::streamsize file_size = is.tellg();
        is.seekg(0, std::ios::beg);
        char *filebuf = new char[file_size];
        // 读入整个文件，处理整行字符串，放到内存中
        std::string line;
        std::vector<record_type> records;
        std::streamsize offset = 0;
        record_type record;

        while (std::getline(is, line)) {
            // 将整行放入内存，并追加 '\0'
            size_t len = line.size();
            line.copy(filebuf + offset, len);
            // 用 \0 替换掉 key/value 中间的 \t 字符
            size_t key_len = line.find('\t');
            if (key_len != std::string::npos) {
                filebuf[offset + key_len] = '\0';
            } else {
                // 为逻辑简便记，抛弃该条记录
                continue;
            }
            filebuf[offset + len + 1] = '\0';
            // 将 dict_word 加入 dict 数组
            record.key = &filebuf[offset];
            char *end;
            record.value = std::strtoul(&filebuf[offset + key_len + 1], &end, 10);
            records.push_back(record);
            offset += len + 1;
        }
        std::sort(records.begin(), records.end(), record_cmp);
        // build trie
        builder_type builder;
        builder.build(&records[0], &records[records.size()]);
        std::ofstream ofs(dbname.c_str(), std::ios::binary | std::ios::trunc);
        builder.write(ofs);
        ofs.close();
        gettimeofday(&end, NULL);
        std::cout << "INFO: Build DB to '" << dbname << "' in "
            << (long)(end.tv_sec - begin.tv_sec) * 1000000 + end.tv_usec - begin.tv_usec
            << "us." << std::endl;
        delete [] filebuf;
    } else {
        dbname = filename;
    }
    if (!dbname.empty()) {
        gettimeofday(&begin, NULL);
        std::ifstream ifs(dbname.c_str(), std::ios::binary);
        size_t ret = the_trie.read(ifs);
        gettimeofday(&end, NULL);
        ifs.close();
        std::cerr << "INFO: Read DB from '" << dbname << "' in "
            << (long)(end.tv_sec - begin.tv_sec) * 1000000 + end.tv_usec - begin.tv_usec
            << "us." << std::endl;
        if (ret == 0) {
            std::cerr << "ERROR: Read DB from '" << dbname << "' failed." << std::endl;
            return 1;
        }
    }
    std::string key;
    uint32_t value = 0;
    std::vector<std::string> test_keys;
    uint32_t test_round = 1;

    // read test keys
    while(true) {
        std::cin >> key;
        if (std::cin.eof()) {
            break;
        }
        test_keys.push_back(key);
    }
    gettimeofday(&begin, NULL);
    for (int i=0; i<test_round; i++) {
        for (int j = 0; j < test_keys.size(); j++) {
            value = the_trie.get(test_keys[j].c_str(), 0);
//            std::cout << test_keys[j] << "\t" << value << std::endl;
        }
    }
    gettimeofday(&end, NULL);
    std::cerr <<  test_round << "*" << test_keys.size() << " get completed in "
        << (long)(end.tv_sec - begin.tv_sec) * 1000000 + end.tv_usec - begin.tv_usec
        << "us, speed: "
        <<  ((test_round*test_keys.size())*1000000.0)/((end.tv_sec - begin.tv_sec) * 1000000 + end.tv_usec - begin.tv_usec)
        << " gets/second." << std::endl;
    return 0;
}
