/*
 Copyright (c) 2016, Wenbo Yang
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 * Neither the name of DictX nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef COM_SUBSTR_SEARCH_H
#define COM_SUBSTR_SEARCH_H

#include <iostream>
#include <fstream>
#include <queue>
#include <stack>

#define DICTX 1
#include "dastrie.h"

namespace dictx {

/// Inner data structure: an efficient double array suffix trie.
typedef dastrie::trie<uint32_t> suffix_trie_type;

/**
 * The common substring search algorithm retrieves all strings (the "result")
 * that have a common substring (longer than a specified length) with the
 * input text (the "query word"), in a finite set of strings
 * (the "dictionary").
 */
class ComSubstrSearch: public suffix_trie_type {

public:
    /**
     * Search query.
     */
    struct Query {
        /// The query word.
        std::string     word;
        /// Minimum common substring length required.
        uint32_t        min_common_len;
        /// Dictionary word shorter than this length will be dropped.
        uint32_t        min_dword_len;
        /// Dictionary word longer than this length will be dropped.
        uint32_t        max_dword_len;
        /// Maximum number of results expected.
        uint32_t        limit;
        /// Suffix trie search priority, default is bread-first search.
        bool            depth_first_search;
        /// Only search for strings with a common prefix string (instead
        /// of common substring).
        bool            com_prefix_only;
        /// Average "limit" to each match position (instead of First
        /// Search First Out)
        /// False Means: Search with "hopeful", if "hope" retrieves enough
        /// results, all "opef" results will be dropped.
        bool            average_limit;
    };

    /**
     * Search result.
     */
    struct Result {
        /// Result string, a dictionary word.
        const char      *dword;
        /// Result value.
        const char      *value;
        /// Start position of the common substring with query word.
        uint32_t        start_pos;
        /// The common substring length.
        uint32_t        common_len;
    };

    /**
     * Constructor.
     * @param suffix_ratio minimum suffix length = floor(suffix ratio * word length)
     *  While building the suffix trie for dictionary words, suffix ratio
     *  controls the minimum suffix length, thus controls the size of
     *  the suffix trie.
     * @param min_suffix Global default minimum length of a dictionary word
     *  suffix string.
     */
    ComSubstrSearch(double suffix_ratio = 0.5, uint32_t min_suffix = 2):
        m_suffix_ratio(suffix_ratio),
        m_min_suffix(min_suffix),
        m_dwords_pool(NULL),
        m_dwords_pool_size(0),
        m_dwords_array(NULL),
        m_dwords_array_size(0),
        m_dwordid_pool(NULL),
        m_dwordid_pool_size(0),
        m_suffix_iindex(NULL),
        m_suffix_iindex_size(0)
    {
        for (int i=0; i < dastrie::NUMCHARS; i++) {
            m_char_table.push_back(i);
        }
        if (m_suffix_ratio <= 0 || m_suffix_ratio >1) {
            // FIXME
        }
    }

    ~ComSubstrSearch() {
        clear();
    }

    /**
     * Common substring search algorithm.
     * @param query
     * @param results
     * @return
     */
    uint32_t search(const Query &query, std::vector<Result> *results)
    {
        if ((NULL == results) || (query.word.size() < query.min_common_len)
                || query.limit == 0) {
            return 0;
        }
        uint32_t result_num = 0;
        results->clear();
        results->reserve(query.limit);
        if (query.com_prefix_only) {
            compre_search(query, results);
        } else {
            Query suffixq(query);
            for (uint32_t i = 0; i <= query.word.size() - query.min_common_len;
                    i++) {
                suffixq.word = query.word.substr(i);
                if (query.average_limit) {
                    suffixq.limit = result_num + query.limit;
                }
                result_num += compre_search(suffixq, results);
            }
        }
        return result_num;
    }

    const std::vector<char>& get_char_table() const {
        return m_char_table;
    }

    /**
     * Each trie node represents a character. The char_table defines all
     * valid characters and the node search priority while traversing through
     * the trie. '\0' is a special character, represents the "end" of a trie
     * record which is a prefix of another trie record.
     * @param char_talble Valid character search table
     * @return zero for success, non-zero for fail
     */
    int set_char_table(const std::vector<char>& char_table) {
        if (char_table.size() <= dastrie::NUMCHARS) {
            size_t i = 0;
            for (; i < char_table.size() && char_table[i] != '\0'; i++) {
            }
            if (i < char_table.size()) {
                m_char_table.assign(char_table.begin(), char_table.end());
                return 0;
            }
        }
        return 1;
    }

    /**
     * @return Number of dictionary words
     */
    uint32_t get_dwords_num() const {
        return m_dwords_array_size;
    }

    /**
     * @return Global default minimum length of a dictionary word suffix
     *  string.
     */
    uint32_t get_min_suffix() const {
        return m_min_suffix;
    }

    /**
     * @return Suffix ratio.
     */
    double get_suffix_ratio() const {
        return m_suffix_ratio;
    }

    /**
     * Build the database for common substring search
     * @param dict_fname Input dictionary file in text
     * @param db_fname Output database file in binary
     * @return zero for success, non-zero for fail
     */
    int build(const std::string &dict_fname, const std::string &db_fname)
    {
        // 计算输入文件大小，并据此申请字符串存储的内存
        std::ifstream is(dict_fname.c_str(), std::ios::binary | std::ios::ate);
        std::streamsize file_size = is.tellg();
        is.seekg(0, std::ios::beg);
        m_dwords_pool = new char[file_size];

        // 读入整个文件，处理整行字符串，放到内存中
        std::string line;
        std::vector<trie_record_type> input_suffix_array;
        std::streamsize offset = 0;
        std::vector<dword_type> dwords_array;

        while (std::getline(is, line)) {
            // 将整行放入内存，并追加 '\0'
            size_t len = line.size();
            line.copy(m_dwords_pool + offset, len);
            // 用 \0 替换掉 key/value 中间的 \t 字符
            size_t key_len = line.find('\t');
            if (key_len != std::string::npos) {
                m_dwords_pool[offset + key_len] = '\0';
            } else {
                // 为逻辑简便记，抛弃该条记录
                continue;
            }
            m_dwords_pool[offset + len + 1] = '\0';

            // 将 dict_word 加入 dict 数组
            dword_type dword;
            dword.offset = offset;
            dword.size = key_len;
            dwords_array.push_back(dword);
            offset += len + 1;
        }
        std::sort(dwords_array.begin(), dwords_array.end());

        m_dwords_array = new dword_type[dwords_array.size()];
        m_dwords_array_size = dwords_array.size();

        for (size_t i=0; i<dwords_array.size(); i++) {
            m_dwords_array[i] = dwords_array[i];
            // std::cout << &m_dwords_pool[m_dict_array[i]] << std::endl;
            // 生成 dict_word 的所有后缀串，加入 suffix 数组
            int min_suffix = dwords_array[i].size*m_suffix_ratio;
            min_suffix = (min_suffix > m_min_suffix) ? min_suffix : m_min_suffix;
            for (int j = 0; j <= static_cast<int>(dwords_array[i].size) - min_suffix; j++) {
                trie_record_type suffix;
                suffix.key = m_dwords_pool + dwords_array[i].offset + j;
                suffix.value = i;
                input_suffix_array.push_back(suffix);
            }
        }

        std::sort(input_suffix_array.begin(), input_suffix_array.end(), suffix_cmp);

        // 将相同 suffix 的 dict_word 加入到该 suffix 的倒排拉链中，并且对
        // input_suffix_array 去重，生成无重复的 suffix_array 用于构建 Trie

        // 在构建 suffix_array 的同时，为每个 suffix 生成紧致的倒排链
        m_dwordid_pool_size = input_suffix_array.size();
        m_dwordid_pool = new uint32_t[m_dwordid_pool_size];
        m_suffix_iindex_size = input_suffix_array.size();
        m_suffix_iindex = new dword_list_type[m_suffix_iindex_size];

        uint32_t                    dwordid;

        size_t i = 0;
        uint32_t wordid_array_offset = 0;
        for (size_t j=1; j < input_suffix_array.size(); j++) {
            // 先处理第一个元素，把倒排链建好，将第一个 suffix 的倒排链指针指向第 0 条链
            if (j == 1) {
                wordid_array_offset = 0;
                m_suffix_iindex[i].offset = wordid_array_offset;
                m_dwordid_pool[wordid_array_offset++] = input_suffix_array[0].value;
                m_suffix_iindex[i].size = 1;
                input_suffix_array[i].value = 0;
            }
            // 当 j 指向的 suffix 与 i 指向的 suffix 相同时，只 append i 的倒排链
            if (std::strcmp(input_suffix_array[j].key, input_suffix_array[i].key) == 0) {
                m_dwordid_pool[wordid_array_offset++] = input_suffix_array[j].value;
                m_suffix_iindex[i].size ++;
            } else {
                // 对前一条倒排链按照 dwordid 排序，其实符合 dwordlen 顺序
                std::sort(&m_dwordid_pool[m_suffix_iindex[i].offset],
                        &m_dwordid_pool[m_suffix_iindex[i].offset + m_suffix_iindex[i].size]);
                ++i;
                m_suffix_iindex[i].offset = wordid_array_offset;
                m_dwordid_pool[wordid_array_offset++] = input_suffix_array[j].value;
                m_suffix_iindex[i].size = 1;
                input_suffix_array[i].value = i;
                input_suffix_array[i].key = input_suffix_array[j].key;
            }
            //std::cout << input_suffix_array[i].key << "," <<  input_suffix_array[i].value << std::endl;
        }
        m_suffix_iindex_size = i + 1;

        // 构建 Trie 树
        trie_builder_type builder;
        builder.build(&input_suffix_array[0], &input_suffix_array[i]);

        if (db_fname.empty()) {
            assign(builder.doublearray(), builder.tail(), builder.table());
        } else {
            std::ofstream ofs(db_fname.c_str(), std::ios::binary | std::ios::trunc);
            builder.write(ofs);
            uint32_t block_size = 0;
            // 写入词组池 BLOCK
            ofs.write("DWDP", 4);
            block_size = offset;
#ifdef DEBUG
            std::cout << "Write m_dwords_pool_block size=" << block_size << std::endl;
#endif
            ofs.write(reinterpret_cast<const char *>(&block_size), sizeof (uint32_t));
            ofs.write(m_dwords_pool, block_size);
            // 写入词组指针 BLOCK
            ofs.write("DWAR", 4);
            block_size = sizeof (dword_type) * m_dwords_array_size;
#ifdef DEBUG
            std::cout << "Write m_dwords_array_block size=" << block_size << std::endl;
#endif
            ofs.write(reinterpret_cast<const char *>(&block_size), sizeof (block_size));
            ofs.write(reinterpret_cast<const char *>(m_dwords_array), block_size);
            // 写入倒排链池 BLOCK
            ofs.write("IDAR", 4);
            block_size = sizeof (uint32_t) * m_dwordid_pool_size;
#ifdef DEBUG
            std::cout << "Write m_dwordid_array_block size=" << block_size << std::endl;
#endif
            ofs.write(reinterpret_cast<const char *>(&block_size), sizeof (block_size));
            ofs.write(reinterpret_cast<const char *>(m_dwordid_pool), block_size);
            // 写入倒排索引 BLOCK
            ofs.write("IIND", 4);
            block_size = sizeof (dword_list_type) * m_suffix_iindex_size;
#ifdef DEBUG
            std::cout << "Write m_suffix_iindex_block size=" << block_size << std::endl;
#endif
            ofs.write(reinterpret_cast<const char *>(&block_size), sizeof (block_size));
            ofs.write(reinterpret_cast<const char *>(m_suffix_iindex), block_size);
            ofs.close();
        }

#ifdef DEBUG
        std::ofstream debug_ofs;
        debug_ofs.open((db_fname + ".dwar").c_str(), std::ios::trunc);
        for (size_t i=0; i< m_dwords_array_size; i++) {
            debug_ofs << i << "\t" << dwords_array[i].size << "\t"
                    << &m_dwords_pool[dwords_array[i].offset] << "\t"
                    <<  &m_dwords_pool[dwords_array[i].offset + dwords_array[i].size +1]
                    << std::endl;
        }
        debug_ofs.close();
        debug_ofs.open((db_fname + ".iind").c_str(), std::ios::trunc);
        for (size_t i=0; i< m_suffix_iindex_size; i++) {
            debug_ofs << i << "\t" << m_suffix_iindex[i].size << "\t" << input_suffix_array[i].key << "\t";
            for (size_t j=0; j<m_suffix_iindex[i].size; j++) {
                debug_ofs << m_dwordid_pool[m_suffix_iindex[i].offset + j] << ",";
            }
            debug_ofs << std::endl;
        }
        debug_ofs.close();
#endif

        return 0;
    }

    /**
     * Read the database from file.
     * @param db_fname Binary database file.
     * @return Size of readed bytes.
     */
    size_t read(const std::string &db_fname) {
        size_t used_size = 0;
        clear();
        std::ifstream ifs(db_fname.c_str(), std::ios::binary);
        if ((!ifs.fail()) && (suffix_trie_type::read(ifs) > 0)) {
            char chunk[4];
            uint32_t block_size = 0;
            // 读入词组池
            ifs.read(chunk, 4);
            if (std::strncmp(chunk, "DWDP", 4) != 0){
                return 0;
            }
            ifs.read(reinterpret_cast<char *>(&block_size), sizeof (uint32_t));
#ifdef DEBUG
            std::cout << "Read m_dwords_pool_block size=" << block_size << std::endl;
#endif
            m_dwords_pool_size = block_size;
            m_dwords_pool = new char[m_dwords_pool_size];
            ifs.read(m_dwords_pool, block_size);
            // 读入词组下标
            ifs.read(chunk, 4);
            if (std::strncmp(chunk, "DWAR", 4) != 0){
                return 0;
            }
            ifs.read(reinterpret_cast<char *>(&block_size), sizeof (block_size));
#ifdef DEBUG
            std::cout << "Read m_dwords_array_block size=" << block_size << std::endl;
#endif
            m_dwords_array_size = block_size / sizeof (dword_type);
            m_dwords_array = new dword_type[m_dwords_array_size];
            ifs.read(reinterpret_cast<char *>(m_dwords_array), block_size);
            // 读入倒排拉链池 BLOCK
            ifs.read(chunk, 4);
            if (std::strncmp(chunk, "IDAR", 4) != 0){
                return 0;
            }
            ifs.read(reinterpret_cast<char *>(&block_size), sizeof (block_size));
#ifdef DEBUG
            std::cout << "Read m_dwordid_array_block size=" << block_size << std::endl;
#endif
            m_dwordid_pool_size = block_size / sizeof (uint32_t);
            m_dwordid_pool = new uint32_t[m_dwordid_pool_size];
            ifs.read(reinterpret_cast<char *>(m_dwordid_pool), block_size);
            // 读入倒排索引
            ifs.read(chunk, 4);
            if (std::strncmp(chunk, "IIND", 4) != 0){
                return 0;
            }
            ifs.read(reinterpret_cast<char *>(&block_size), sizeof (block_size));
#ifdef DEBUG
            std::cout << "Read m_suffix_iindex_block size=" << block_size << std::endl;
#endif
            m_suffix_iindex_size = block_size / sizeof (dword_list_type);
            m_suffix_iindex = new dword_list_type[m_suffix_iindex_size];
            ifs.read(reinterpret_cast<char *>(m_suffix_iindex), block_size);
            used_size = ifs.tellg();
        }
        return used_size;
    }



protected:
    /// dict_word 类型，为了减少 8 字节指针的使用和便于反序列化，使用 4 字节偏移，长度本身可以考虑用 uint8_t
    /// 但其实这个数据结构占用空间不大，暂不考虑优化
    struct dword_type {
        uint32_t    offset;
        uint32_t    size;
        bool operator < (const dword_type &rv) const
        {
            return size < rv.size;
        }
    };
    /// suffix 倒排索引拉链的类型，为了减少 8 字节指针的使用和便于反序列化，使用 4 字节偏移
    struct dword_list_type {
        uint32_t offset;
        uint32_t size;
    };

    /// Trie 树的 builder 类型
    typedef dastrie::builder<char *, uint32_t>      trie_builder_type;
    typedef trie_builder_type::record_type          trie_record_type;

    // 对 suffix 数组进行排序
    static bool suffix_cmp(const trie_record_type &r1, const trie_record_type &r2)
    {
        return std::strcmp(r1.key, r2.key) < 0;
    }

    /**
     *  倒排拉链本身按照 dword 长度有序，这里对倒排拉链进行二分查找，在 [first, last) 范围内
     *  寻找大于等于 dwordlen 的第一个元素位置。为保证算法正确性，这里使用了
     *  std::lower_bound 相似的算法实现。
     *  @param  first           查找范围的第一个元素
     *  @param  last            查找范围最后一个元素的后一个位置（不在查找范围内）
     *  @param  dwordlen        要查找的 dwordlen
     *  @return 返回成功的查找位置，或者 last 代表未找到
     */
    inline const uint32_t * lower_bound(const uint32_t *first, const uint32_t *last,
            uint32_t dwordlen) {
        const uint32_t *it;
        int32_t count = last - first;
        int32_t step = 0;
        while (count > 0) {
            it = first;
            step = count / 2;
            it += step;
            if (m_dwords_array[*it].size < dwordlen) {
                first = ++it;
                count -= step + 1;
            } else {
                count = step;
            }
        }
        return first;
    }

    size_t retrieve_dword(const Query &query,
                size_t match_len,
                uint32_t suffixid,
                size_t suffix_len,
                std::vector<Result> *results) {
            size_t result_num = 0;
            if ((suffixid < m_suffix_iindex_size) && (results->size() < query.limit)) {
                const uint32_t *dword_list = m_dwordid_pool + m_suffix_iindex[suffixid].offset;
                uint32_t dword_list_size = m_suffix_iindex[suffixid].size;
                const uint32_t *dword_list_end = dword_list + dword_list_size;
                const uint32_t *i = lower_bound(dword_list, dword_list_end, query.min_dword_len);
                for (; i<dword_list_end; i++) {
                    uint32_t dwordid = *i;
                    // 因为是有序数组，所以直接跳出
                    if (m_dwords_array[dwordid].size > query.max_dword_len) {
                        break;
                    }
                    Result result;
                    result.dword = m_dwords_pool + m_dwords_array[dwordid].offset;
                    result.start_pos = m_dwords_array[dwordid].size - suffix_len;
                    result.common_len = match_len;
                    result.value = m_dwords_pool + m_dwords_array[dwordid].offset + m_dwords_array[dwordid].size + 1;
                    results->push_back(result);
#ifdef DEBUG
                    std::cout << "push word[" << results->size()-1 << "]='" << result.dword << "'" << std::endl;
#endif
                    result_num++;
                    if (results->size() >= query.limit) {
                        break;
                    }
                }
            } else {
                // FIXME: THIS SHOULD NOT HAPPEN
                std::cout << "FATAL: suffixid(" << suffixid << ") exceed suffix_iindex_size("
                        << m_suffix_iindex_size << ")" << std::endl;
            }
            return result_num;
        }

    /**
     *  对输入 key 进行共同前缀匹配搜索，当前缀匹配长度超过 min_match 时，召回
     *  匹配到的 Trie 记录（即 dict_word_suffix），然后获取 dict_word_suffix
     *  的倒排拉链，筛选出长度不大于 max_dword_len 的 dict_word 返回。
     *  @param  query           查询请求
     *  @param  results         输出结果的数组
     *  @note Trie 树的一种特殊结构会影响搜索过程。当 Trie 记录存在前缀覆盖时，比如
     *  youthful, youthfully, youthfulness，Trie 树会给 youthful 加上 \0 节点，代表
     *  整字的匹配，所以在遍历时，可能会走到 \0，需要进行一些特殊处理。
     */
    size_type compre_search(const Query &query, std::vector<Result> *results)
    {
        size_type   result_num = 0;
        // 最小匹配长度不能大于 query.word 的长度，或者 query.max_dword_len
        if ((query.min_common_len > query.word.size())
                || (query.min_common_len > query.max_dword_len)) {
            return result_num;
        }

        size_type   cur = dastrie::INITIAL_INDEX;
        // 如果 Trie 树为空，返回 0
        base_type base = get_base(cur);
        if (base < 0) {
            return result_num;
        }

        size_type               offset = 0;
        size_t                  match_len = 0;
        std::stack<size_type>   trie_index_stack;

        // 开始进行最长共同前缀匹配
        for (match_len=0; match_len < query.word.size() && match_len <= query.max_dword_len; ) {
            // Trie 树继续下溯，使用尚未匹配的下一个字符
            cur = descend(cur, query.word.c_str()[match_len]);
            // 判断下个字符是否匹配
            if (dastrie::INVALID_INDEX == cur) {
                // case 1：没匹配到任何节点，跳出循环
                break;
            } else {
                match_len++;
                base_type base = get_base(cur);

                if (base < 0) {
                    // case 2: 匹配到叶子节点，处理与 TAIL 的匹配长度，输出叶子节点的结果
                    offset = (size_type) -base;
                    // 直接去检查 TAIL，这时候 match_len 应该指向剩余未进行匹配的字符，继续与 TAIL 完成剩余字符的匹配
                    uint32_t match_len_save = match_len;
                    m_tail.seekg(offset);
                    uint32_t tail_len = m_tail.strlen();
                    uint32_t suffix_len = match_len + tail_len;
                    std::string word_remain = query.word.substr(match_len);
                    match_len += m_tail.match_string_prefix(word_remain.c_str());
                    // case 2.1: 加上TAIL，已匹配前缀长度超过 query.min_common_len，给出搜索结果
                    if (match_len >= query.min_common_len) {
                        offset += tail_len + 1;
                        m_tail.seekg(offset);
                        uint32_t suffixid = 0;
                        m_tail >> suffixid;
                        result_num += retrieve_dword(query, match_len, suffixid, suffix_len, results);
                    }
                    match_len = match_len_save - 1;
                    break;
                }
                // continue case: 在匹配到某节点，它不是叶子节点的情况下，才需要继续沿树下溯
                // 当相同前缀长度超过 query.min_common_len 时，推到栈中用于回溯
                if (match_len >= query.min_common_len) {
                    trie_index_stack.push(cur);
                }
            }
        }

        // 当回溯 stack 不为空时
        // 1. 可能是 match_len == query.word.size() 或者 match_len > query.max_dword_len 循环终止
        // 2. 可能是 case 1
        // 3. 可能是 case 2.1
        // 遍历方式：
        // 第一层遍历：回溯；从最深的已匹配节点开始，通过 trie_index_stack 回溯所有超过 query.min_common_len 深度的已匹配节点
        // 第二层遍历：下溯；遍历已匹配节点到 query.max_dword_len 深度的所有子节点
        size_type except = dastrie::INVALID_INDEX;
        while (!trie_index_stack.empty()) {
            cur = trie_index_stack.top();
            if (query.depth_first_search) {
                result_num += df_traversal(query, cur, match_len, except, results);
            } else {
                result_num += bf_traversal(query, cur, match_len, except, results);
            }
            except = cur;
            trie_index_stack.pop();
            match_len --;
        }
        return result_num;
    }

    struct node_info_type {
        size_type       cur;
        uint32_t   suffix_len;
    };

    /**
     *  对已经匹配到的共同前缀，广度优先遍历 Trie 树，拿到该前缀下的所有满足条件的 Trie record
     *  并检索该 record 对应的倒排链，拿出所有满足条件的 dword
     *  @param  query           查询请求，主要使用其中的一些限定条件
     *  @param  start_cur       要进行子树遍历的起始节点
     *  @param  match_len       已经匹配到的共同前缀长度
     *  @param  depth           当前起始节点的深度
     *  @param  except          要跳过的子树分支
     *  @param  results         输出结果的数组
     */
    size_t bf_traversal(const Query &query, size_type start_cur,
            uint32_t match_len, size_type except,
            std::vector<Result> *results) {
        if ((match_len > query.max_dword_len) || (results->size() >= query.limit)) {
            return 0;
        }
        size_t result_num = 0;
        node_info_type cur_node = {start_cur, match_len};
        std::queue<node_info_type> node_queue;
        node_queue.push(cur_node);

        while ((!node_queue.empty()) && (results->size() < query.limit)) {
            // 取出队列顶端的节点，如果是叶子节点，就输出节点内容，如果不是叶子节点，就去遍历它的子节点
            cur_node = node_queue.front();
            node_queue.pop();
            base_type base = get_base(cur_node.cur);
            // 如果是叶子节点，就标记为一个结果
            if (base < 0) {
                size_type offset = (size_type) -base;
                m_tail.seekg(offset);
                size_type tail_len = m_tail.strlen();
                // 当走到 \0 节点时，需要进行一点特殊处理
                size_type suffix_len = cur_node.suffix_len + tail_len;
                if (suffix_len <= query.max_dword_len) {
                    offset += tail_len + 1;
                    m_tail.seekg(offset);
                    uint32_t suffixid = 0;
                    m_tail >> suffixid;
                    result_num += retrieve_dword(query, match_len, suffixid, suffix_len,
                            results);
                }
            } else if (cur_node.suffix_len <= query.max_dword_len) {
                std::vector<char>::iterator it = m_char_table.begin();
                std::vector<char>::iterator end = m_char_table.end();
                if (cur_node.suffix_len == query.max_dword_len) {
                    end = m_char_table.begin() + 1;
                }
                for (; it < end; it++) {
                    size_type cur = descend(cur_node.cur, *it);
                    if (cur == except) {
                        continue;
                    }
                    if (cur != dastrie::INVALID_INDEX) {
                        node_info_type child_node(cur_node);
                        child_node.cur = cur;
                        // 因为 '\0' 作为占位符不占字长，这里需要做个特殊处理，才好计算共同前缀的位置
                        if ((*it) != '\0') {
                            child_node.suffix_len++;
                        }
#ifdef DEBUG
                        std::cout << "node_queue.push char '" << *it << "' suffix_len " << child_node.suffix_len << std::endl;
#endif
                        node_queue.push(child_node);
                    }
                }
            }
        }
        return result_num;
    }

    /**
     *  对已经匹配到的共同前缀，深度优先遍历 Trie 树，拿到该前缀下的所有满足条件的 Trie record
     *  并检索该 record 对应的倒排链，拿出所有满足条件的 dword
     *  @param  query           查询请求，主要使用其中的一些限定条件
     *  @param  start_cur       要进行子树遍历的起始节点
     *  @param  match_len       已经匹配到的共同前缀长度
     *  @param  depth           当前起始节点的深度
     *  @param  except          要跳过的子树分支
     *  @param  results         输出结果的数组
     */
    size_t df_traversal(const Query &query, size_type start_cur,
            uint32_t match_len, size_type except,
            std::vector<Result> *results) {
        if ((match_len > query.max_dword_len) || (results->size() >= query.limit)) {
            return 0;
        }
        size_t result_num = 0;
        node_info_type cur_node = {start_cur, match_len};
        std::stack<node_info_type> node_stack;
        node_stack.push(cur_node);

        while ((!node_stack.empty()) && (results->size() < query.limit)) {
            // 取出队列顶端的节点，如果是叶子节点，就输出节点内容，如果不是叶子节点，就去遍历它的子节点
            cur_node = node_stack.top();
            node_stack.pop();
            base_type base = get_base(cur_node.cur);
            // 如果是叶子节点，就标记为一个结果
            if (base < 0) {
                size_type offset = (size_type) -base;
                m_tail.seekg(offset);
                size_type tail_len = m_tail.strlen();
                // 当走到 \0 节点时，需要进行一点特殊处理
                size_type suffix_len = cur_node.suffix_len + tail_len;
                if (suffix_len <= query.max_dword_len) {
                    offset += tail_len + 1;
                    m_tail.seekg(offset);
                    uint32_t suffixid = 0;
                    m_tail >> suffixid;
                    result_num += retrieve_dword(query, match_len, suffixid, suffix_len,
                            results);
                }
            } else if (cur_node.suffix_len <= query.max_dword_len) {
                std::vector<char>::reverse_iterator it;
                for (it=m_char_table.rbegin(); it < m_char_table.rend(); it++) {
                    size_type cur = descend(cur_node.cur, *it);
                    if (cur == except) {
                        continue;
                    }
                    if (cur != dastrie::INVALID_INDEX) {
                        node_info_type child_node(cur_node);
                        child_node.cur = cur;
                        // 因为 '\0' 作为占位符不占字长，这里需要做个特殊处理，才好计算共同前缀的位置
                        if ((*it) != '\0') {
                            child_node.suffix_len++;
                        }
                        node_stack.push(child_node);
                    }
                }
            }
        }
        return result_num;
    }

    void clear() {
        if (NULL != m_dwords_pool) {
            delete [] m_dwords_pool;
            m_dwords_pool = NULL;
            m_dwords_pool_size = 0;
        }
        if (NULL != m_dwords_array) {
            delete [] m_dwords_array;
            m_dwords_array = NULL;
            m_dwords_array_size = 0;
        }
        if (NULL != m_dwordid_pool) {
            delete [] m_dwordid_pool;
            m_dwordid_pool = NULL;
            m_dwordid_pool_size = 0;
        }
        if (NULL != m_suffix_iindex) {
            delete [] m_suffix_iindex;
            m_suffix_iindex = NULL;
            m_suffix_iindex_size = 0;
        }
    }

protected:
    /// minimum suffix length = floor(suffix ratio * word length)
    /// While building the suffix trie for dictionary words, suffix ratio
    /// controls the minimum suffix length, thus controls the size of
    /// the suffix trie.
    double                          m_suffix_ratio;
    /// minimum length of a dictionary word suffix string.
    uint32_t                        m_min_suffix;
    /// Valid character search table, default to 0-255
    std::vector<char>               m_char_table;
    /// Dictionary word string pool.
    char                            *m_dwords_pool;
    /// Size of dictionary word string pool.
    uint32_t                        m_dwords_pool_size;
    /// Dictionary words array. The array index is "dwordid".
    dword_type                      *m_dwords_array;
    /// Size of dictionary words array.
    uint32_t                        m_dwords_array_size;
    /// Diciontary word id pool. It stores all inverted index list.
    uint32_t                        *m_dwordid_pool;
    /// Size of diciontary word id pool.
    uint32_t                        m_dwordid_pool_size;
    /// Suffix inverted index list heads array. The array index is "suffixid".
    dword_list_type                 *m_suffix_iindex;
    /// Size of suffix inverted index list heads.
    uint32_t                        m_suffix_iindex_size;
};


}; // namespace dictx

#endif // COM_SUBSTR_SEARCH_H
