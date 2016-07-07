#include "com_substr_search.h"

#include <sys/time.h>
#include <unistd.h>

using namespace dictx;

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " File" << std::endl;
        return 1;
    }
    std::string filename = argv[1];
    std::string dbname = "";

    struct timeval begin, end;

    ComSubstrSearch css;

    if (filename.find(".db") == std::string::npos) {
        if (argc == 3) {
            dbname = argv[2];
        }
        gettimeofday(&begin, NULL);
        css.build(filename, dbname);
        gettimeofday(&end, NULL);
        std::cout << "INFO: Build DB to '" << dbname << "' in "
            << (long)(end.tv_sec - begin.tv_sec) * 1000000 + end.tv_usec - begin.tv_usec
            << "us." << std::endl;
    } else {
        dbname = filename;
    }
    if (!dbname.empty()) {
        gettimeofday(&begin, NULL);
        size_t ret = css.read(dbname);
        gettimeofday(&end, NULL);
        std::cout << "INFO: Read DB from '" << dbname << "' in "
            << (long)(end.tv_sec - begin.tv_sec) * 1000000 + end.tv_usec - begin.tv_usec
            << "us." << std::endl;
        if (ret == 0) {
            std::cerr << "ERROR: Read DB from '" << dbname << "' failed." << std::endl;
            return 1;
        }
    }
    // 设置合理的字符集 char_table，加快遍历速度
    std::vector<char> char_table;
    for (char i='a'; i<='z'; i++) {
        char_table.push_back(i);
    }
    css.set_char_table(char_table);

    ComSubstrSearch::Query query;
    query.word = "youthe";
    query.min_common_len = 4;
    query.max_dword_len = 8;
    query.limit = 1000;

    while(true) {
        std::cout << "Start search by INPUT {query_word, min_common_len, min_dword_len, max_dword_len, limit}: ";
        int a, b, c;
        std::cin >> query.word >> a >> b >> c >> query.limit;
        if (std::cin.eof()) {
            break;
        }
        query.min_common_len = a;
        query.min_dword_len = b;
        query.max_dword_len = c;
        query.depth_first_search = false;
        query.com_prefix_only = false;
        query.average_limit = true;

        std::cout << "####################################################\n";
        std::cout << "# Start searching with query {\n#    word='" << query.word
            << "'\n#    min_common_len=" << static_cast<size_t>(query.min_common_len)
            << "\n#    max_dword_len=" << static_cast<size_t>(query.max_dword_len)
            << "\n#    min_dword_len=" << static_cast<size_t>(query.min_dword_len)
            << "\n#    limit=" << query.limit
            << "\n#}" << std::endl;

        std::vector<ComSubstrSearch::Result> results;
        gettimeofday(&begin, NULL);
        css.search(query, &results);
        gettimeofday(&end, NULL);
        std::cout << "# Search '" << query.word << "' completed in "
            << (long)(end.tv_sec - begin.tv_sec) * 1000000 + end.tv_usec - begin.tv_usec
            << "us with " << results.size() << " results: " << std::endl;
        std::vector<ComSubstrSearch::Result>::iterator it = results.begin();
        for (size_t i=0; it < results.end(); it++) {
            std::string word = it->dword;
            std::cout << "results[" << i++ << "]\t" << word << "\t";
            word.insert(static_cast<size_t>(it->start_pos), "[");
            word.insert(static_cast<size_t>(it->start_pos + it->common_len + 1), "]");
            std::cout << word << "\t" << it->value << "\n";
        }
        std::cout << std::endl;
        std::cout << "####################################################\n\n";
    }

    return 0;
}
