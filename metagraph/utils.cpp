#include "utils.hpp"

#include <fstream>
#include <algorithm>

#include "kmer.hpp"

const size_t kBitsPerDigit = 17;


namespace utils {

std::string join_strings(const std::vector<std::string> &strings,
                         const std::string &delimiter) {
    if (!strings.size())
        return "";

    if (strings.size() == 1)
        return strings[0];

    std::string result = strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        result += delimiter + strings[i];
    }
    return result;
}

std::vector<std::string> split_string(const std::string &string,
                                      const std::string &delimiter) {
    if (!string.size())
        return {};

    if (!delimiter.size())
        return { string, };

    std::vector<std::string> result;

    size_t current_pos = 0;
    size_t delimiter_pos;

    while ((delimiter_pos = string.find(delimiter, current_pos))
                                             != std::string::npos) {
        if (delimiter_pos > current_pos)
            result.push_back(string.substr(current_pos, delimiter_pos - current_pos));
        current_pos = delimiter_pos + delimiter.size();
    }
    if (current_pos < string.size()) {
        result.push_back(string.substr(current_pos));
    }
    return result;
}


uint64_t kFromFile(const std::string &infbase) {
    uint64_t k = 0;
    std::ifstream instream((infbase + ".F.dbg").c_str()); 
    std::string line;
    size_t mode = 0;
    while (std::getline(instream, line)) {
        if (strcmp(line.c_str(), ">F") == 0 || strcmp(line.c_str(), ">p") == 0) {
            mode = 1;
        } else if (strcmp(line.c_str(), ">k") == 0) {
            mode = 2;
        } else {
            if (mode == 2) {
                k = strtoul(line.c_str(), NULL, 10);
            } else if (mode != 1) {
                fprintf(stderr, "ERROR: input file corrupted\n");
                exit(1);
            }
        }
    }
    instream.close();
    return k;
}

/**
* This function takes a pointer to a graph structure G1 and a corresponding node index k1_node
* as well as a pointer to a second graph structure G2 and a corresponding node index k2_node. It
* returns a pair of bool with the first value set to true if G1(k1_node) < G2(k2_node) and the
* second value set to true if G2(k2_node) < G1(k1_node).
*/
std::pair<bool, bool> compare_nodes(const DBG_succ *G1, uint64_t k1_node,
                                    const DBG_succ *G2, uint64_t k2_node) {
    assert(G1->get_k() == G2->get_k());

    std::pair<TAlphabet, uint64_t> k1_val;
    std::pair<TAlphabet, uint64_t> k2_val;

    for (uint64_t curr_k = 0; curr_k < G1->get_k(); ++curr_k) {
        k1_val = G1->get_minus_k_value(k1_node, 0);
        k2_val = G2->get_minus_k_value(k2_node, 0);
        if (k1_val.first != k2_val.first) {
            break;
        }
        k1_node = k1_val.second;
        k2_node = k2_val.second;
    }

    return std::make_pair(k1_val.first < k2_val.first, k2_val.first < k1_val.first);
}


/**
 *  Returns the input file type, given a filename
 */
std::string get_filetype(const std::string &fname) {
    size_t dotind = fname.rfind(".");
    if (dotind == std::string::npos)
        return "";

    std::string ext = fname.substr(dotind);

    if (ext == ".gz") {
        size_t nextind = fname.substr(0, dotind - 1).rfind(".");
        if (nextind == std::string::npos)
            return "";

        ext = fname.substr(nextind, dotind - nextind);
    }

    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".vcf") {
        return "VCF";
    } else if ((ext == ".fq") || (ext == ".fastq")) {
        return "FASTQ";
    } else {
        return "FASTA";
    }
}

/**
 * Given a minimum number of splits,
 * generate a list of suffices from the alphabet.
 */
std::deque<std::string> generate_strings(const std::string &alphabet,
                                         size_t length) {

    std::deque<std::string> suffices = { "" };
    while (suffices[0].length() < length) {
        for (const char c : alphabet) {
            suffices.push_back(c + suffices[0]);
        }
        suffices.pop_front();
    }
    assert(suffices.size() == std::pow(alphabet.size(), length));
    return suffices;
}


template <size_t bits_per_digit>
void counting_sort(KMer *begin, KMer *end) {
    assert(end >= begin);

    const uint64_t max_digit = 1llu << bits_per_digit;

    std::array<size_t, max_digit> count {};
    for (const KMer *it = begin; it != end; ++it) {
        count[it->get_digit<bits_per_digit>(0)]++;
    }

    std::vector<KMer> unsorted(begin, end);
    std::partial_sum(count.begin(), count.end(), count.begin());

    for (auto it = unsorted.rbegin(); it != unsorted.rend(); ++it) {
        begin[--count[it->get_digit<bits_per_digit>(0)]] = *it;
    }
}


template <size_t bits_per_digit>
void radix_sort(KMer *begin, KMer *end, size_t num_digits) {
    const uint64_t max_digit = 1llu << bits_per_digit;

    std::vector<std::array<size_t, max_digit>> counts(
        num_digits,
        std::array<size_t, max_digit>{}
    );
    for (const KMer *it = begin; it != end; ++it) {
        for (size_t digit = 0; digit < num_digits; ++digit) {
            counts[digit][it->get_digit<bits_per_digit>(digit)]++;
        }
    }

    std::vector<KMer> unsorted(begin, end);
    for (size_t digit = 0; digit < num_digits; ++digit) {
        auto &count = counts[digit];
        std::partial_sum(count.begin(), count.end(), count.begin());

        for (auto it = unsorted.rbegin(); it != unsorted.rend(); ++it) {
            begin[--count[it->get_digit<bits_per_digit>(digit)]] = *it;
        }
    }
}

void radix_sort(std::vector<KMer> &data, size_t k) {
    radix_sort<kBitsPerDigit>(data.data(), data.data() + data.size(),
                              ((k + 1) * kBitsPerChar - 1) / kBitsPerDigit + 1);
}


template <size_t bits_per_digit>
void bucket_sort(KMer *begin, KMer *end, size_t num_digits) {
    const uint64_t num_buckets = 1llu << bits_per_digit;

    std::array<size_t, num_buckets> count {};

    for (const KMer *it = begin; it != end; ++it) {
        count[it->get_digit<bits_per_digit>(num_digits - 1)]++;
    }
    std::partial_sum(count.begin(), count.end(), count.begin());
    std::array<size_t, num_buckets + 1> bucket_bins {};
    std::copy(count.begin(), count.end(), bucket_bins.begin() + 1);

    for (size_t i = 0; begin + i < end; ++i) {
        size_t bucket = begin[i].get_digit<bits_per_digit>(num_digits - 1);
        while (i < bucket_bins[bucket] || i >= bucket_bins[bucket + 1]) {
            std::swap(begin[i], begin[--count[bucket]]);
            bucket = begin[i].get_digit<bits_per_digit>(num_digits - 1);
        }
    }
    if (num_digits == 1)
        return;

    for (size_t b = 0; b < num_buckets; ++b) {
        const size_t num_bits_for_counting = 20;
        if (bucket_bins[b + 1] - bucket_bins[b] < 100'000) {
            std::sort(begin + bucket_bins[b],
                      begin + bucket_bins[b + 1]);
        } else if ((num_digits - 1) * bits_per_digit <= num_bits_for_counting
                    && bucket_bins[b + 1] - bucket_bins[b] < 800'000) {
            counting_sort<num_bits_for_counting>(begin + bucket_bins[b],
                                                 begin + bucket_bins[b + 1]);
        } else {
            bucket_sort<bits_per_digit>(begin + bucket_bins[b],
                                        begin + bucket_bins[b + 1],
                                        num_digits - 1);
        }
    }
}

void bucket_sort(std::vector<KMer> &data, size_t k) {
    const size_t bits_per_digit = 4;
    bucket_sort<bits_per_digit>(data.data(),
                                data.data() + data.size(),
                                ((k + 1) * kBitsPerChar - 1) / bits_per_digit + 1);
}


ThreadPool::ThreadPool(size_t num_workers, size_t max_num_tasks)
      : max_num_tasks_(std::min(max_num_tasks, num_workers * 5)), stop_(false) {
    initialize(num_workers);
}

void ThreadPool::join() {
    size_t num_workers = workers.size();

    if (!num_workers) {
        return;
    } else {
        std::lock_guard<std::mutex> lock(queue_mutex);
        assert(!joining_);
        joining_ = true;
    }
    empty_condition.notify_all();

    for (std::thread &worker : workers) {
        worker.join();
    }
    workers.clear();

    if (!stop_)
        initialize(num_workers);
}

ThreadPool::~ThreadPool() {
    stop_ = true;
    join();
}

void ThreadPool::initialize(size_t num_workers) {
    assert(!stop_);
    assert(workers.size() == 0);
    joining_ = false;

    if (!num_workers)
        return;

    for(size_t i = 0; i < num_workers; ++i) {
        workers.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->empty_condition.wait(lock, [this]() {
                        return this->joining_ || !this->tasks.empty();
                    });
                    if (this->tasks.empty())
                        return;

                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                    full_condition.notify_one();
                }

                task();
            }
        });
    }
}

/**
 * Break the sequence to kmers and extend the temporary kmers storage.
 */
void sequence_to_kmers(std::vector<TAlphabet>&& seq,
                       size_t k,
                       std::vector<KMer> *kmers,
                       const std::vector<TAlphabet> &suffix) {
    assert(k);
    assert(suffix.size() <= k);

    if (seq.size() < k + 1)
        return;

    // based on performance comparison
    // for KMer::pack_kmer and KMer::update_kmer
    if (suffix.size() > 1) {
        for (size_t i = 0; i < seq.size() - k; ++i) {
            if (std::equal(suffix.begin(), suffix.end(),
                           &seq[i + k] - suffix.size())) {
                kmers->emplace_back(&seq[i], k + 1);
            }
        }
    } else {
        // initialize and add the first kmer from sequence
        auto kmer = KMer::pack_kmer(seq.data(), k + 1);

        if (std::equal(suffix.begin(), suffix.end(),
                       &seq[k] - suffix.size())) {
            kmers->emplace_back(kmer);
        }

        // add all other kmers
        for (size_t i = 1; i < seq.size() - k; ++i) {
            KMer::update_kmer(k, seq[i + k], seq[i + k - 1], &kmer);

            if (std::equal(suffix.begin(), suffix.end(),
                           &seq[i + k] - suffix.size())) {
                kmers->emplace_back(kmer);
            }
        }
    }
}

/**
 * Break the sequence to kmers and extend the temporary kmers storage.
 */
void sequence_to_kmers(const std::string &sequence,
                       size_t k,
                       std::vector<KMer> *kmers,
                       const std::vector<TAlphabet> &suffix) {
    assert(k);
    assert(suffix.size() <= k);

    if (sequence.size() < k)
        return;

    // encode sequence
    size_t dummy_prefix_size = suffix.size() > 0 ? k : 1;

    std::vector<TAlphabet> seq(sequence.size() + dummy_prefix_size + 1);

    for (size_t i = 0; i < dummy_prefix_size; ++i) {
        seq[i] = DBG_succ::encode('$');
    }
    std::transform(sequence.begin(), sequence.end(),
                   &seq[dummy_prefix_size], DBG_succ::encode);
    seq.back() = DBG_succ::encode('$');

    sequence_to_kmers(std::move(seq), k, kmers, suffix);
}

} // namespace utils
