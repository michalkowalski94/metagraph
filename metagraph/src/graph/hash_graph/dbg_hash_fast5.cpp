#include "dbg_hash_fast5.hpp"

#include <cassert>

#include <tsl/ordered_set.h>
//#include <tsl/robin_map.h>
#include <libmaus2/util/NumberSerialisation.hpp>

#include "serialization.hpp"
#include "bit_vector.hpp"
#include "utils.hpp"


template <typename KMER = KmerExtractor2Bit::Kmer64>
class DBGHashFast5Impl : public DBGHashFast5::DBGHashFast5Interface {
    using Kmer = KMER;
    using TAlphabet = KmerExtractor2Bit::TAlphabet;
    using KmerPrefix = typename KMER::WordType;

#if _PROTEIN_GRAPH
    using Bits = uint32_t;
#elif _DNA_CASE_SENSITIVE_GRAPH
    using Bits = uint16_t;
#elif _DNA5_GRAPH
    using Bits = unsigned char;
#elif _DNA_GRAPH
    using Bits = unsigned char;
#else
    static_assert(false, "invalid or undefined alphabet");
#endif

    using KmerIndex = tsl::ordered_set<KmerPrefix,
                                     utils::Hash<KmerPrefix>,
                                     std::equal_to<KmerPrefix>,
                                     std::allocator<KmerPrefix>,
                                     //std::deque<KmerPrefix, std::allocator<KmerPrefix>>,
                                     std::vector<KmerPrefix, std::allocator<KmerPrefix>>,
                                     std::uint64_t>;

    using KmerIterator = typename KmerIndex::iterator;
    using KmerConstIterator = typename KmerIndex::const_iterator;

  public:
    explicit DBGHashFast5Impl(size_t k,
                              bool canonical_mode,
                              bool packed_serialization);

    // Insert sequence to graph and mask the inserted nodes if |nodes_inserted|
    // is passed. If passed, |nodes_inserted| must have length equal
    // to the number of nodes in graph.
    void add_sequence(const std::string &sequence,
                      bit_vector_dyn *nodes_inserted);

    // Traverse graph mapping sequence to the graph nodes
    // and run callback for each node until the termination condition is satisfied
    void map_to_nodes(const std::string &sequence,
                      const std::function<void(node_index)> &callback,
                      const std::function<bool()> &terminate) const;

    // Traverse graph mapping sequence to the graph nodes
    // and run callback for each node until the termination condition is satisfied.
    // Guarantees that nodes are called in the same order as the input sequence.
    // In canonical mode, non-canonical k-mers are NOT mapped to canonical ones
    void map_to_nodes_sequentially(std::string::const_iterator begin,
                                   std::string::const_iterator end,
                                   const std::function<void(node_index)> &callback,
                                   const std::function<bool()> &terminate) const;

    void call_outgoing_kmers(node_index node,
                             const OutgoingEdgeCallback &callback) const;

    void call_incoming_kmers(node_index node,
                             const IncomingEdgeCallback &callback) const;

    void call_nodes(const std::function<void(node_index)> &callback,
                                   const std::function<bool()> &stop_early) const;

    // Traverse the outgoing edge
    node_index traverse(node_index node, char next_char) const;
    // Traverse the incoming edge
    node_index traverse_back(node_index node, char prev_char) const;

    // Given a node index, call the target nodes of all edges outgoing from it.
    void adjacent_outgoing_nodes(node_index node,
                                 const std::function<void(node_index)> &callback) const;
    // Given a node index, call the source nodes of all edges incoming to it.
    void adjacent_incoming_nodes(node_index node,
                                 const std::function<void(node_index)> &callback) const;

    size_t outdegree(node_index) const;
    bool has_single_outgoing(node_index) const;
    bool has_multiple_outgoing(node_index) const;

    size_t indegree(node_index) const;
    bool has_no_incoming(node_index) const;
    bool has_single_incoming(node_index) const;

    node_index kmer_to_node(const std::string &kmer) const;

    std::string get_node_sequence(node_index node) const;

    size_t get_k() const { return k_; }
    bool is_canonical_mode() const { return canonical_mode_; }

    uint64_t num_nodes() const { return kmers_.size() << KMER::kBitsPerChar; }

    void serialize(std::ostream &out) const;
    void serialize(const std::string &filename) const;

    bool load(std::istream &in);
    bool load(const std::string &filename);

    std::string file_extension() const { return kExtension; }

    bool operator==(const DeBruijnGraph &other) const;

    const std::string& alphabet() const { return seq_encoder_.alphabet; }

    bool in_graph(node_index node) const;

  private:
    Vector<Kmer> sequence_to_kmers(const std::string &sequence, bool canonical = false) const {
        return seq_encoder_.sequence_to_kmers<Kmer>(sequence, k_, canonical);
    }

    KmerIterator find_kmer(const KmerPrefix &kmer_prefix) {
        KmerIterator find = kmers_.find(kmer_prefix);
        assert(find == kmers_.end() || !in_graph(get_index(find)) || KMER(kmer_prefix) == get_kmer(get_index(find), find));
        return find;
    }

    KmerConstIterator find_kmer(const KmerPrefix &kmer_prefix) const {
        KmerConstIterator find = kmers_.find(kmer_prefix);
        assert(find == kmers_.end() || !in_graph(get_index(find)) || KMER(kmer_prefix) == get_kmer(get_index(find), find));
        return find;
    }

    bool kmers_overlap(const Kmer &out_kmer, const Kmer &in_kmer) const {
        KMER overlap = out_kmer;
        overlap.to_next(k_, in_kmer[k_ - 1]);
        return overlap == in_kmer;
    }

    node_index get_index(const KmerConstIterator &iter) const;
    node_index get_index(const Kmer &kmer) const;

    size_t get_bit_index(const KmerConstIterator &iter) const {
        return iter - kmers_.begin();
        //return (get_index(iter) - 1) >> KMER::kBitsPerChar;
    }

    KmerIterator get_iter(node_index node);
    KmerConstIterator get_const_iter(node_index node) const;

    const Kmer get_kmer(node_index node,
                        KmerConstIterator kmer_iter) const;

    size_t k_;
    bool canonical_mode_;

    KmerIndex kmers_;
    std::vector<Bits> bits_;
    std::vector<node_index> next_node_;
    std::vector<char> next_char_;
    std::vector<bool> may_contain_source_kmer_;
    KmerExtractor2Bit seq_encoder_;

    bool packed_serialization_;

    const KmerPrefix kIgnoreLastCharMask;
    const Bits kLastCharMask = (Bits(1) << seq_encoder_.alphabet.size()) - 1;
    const Bits kIncomingEdgesMask = kLastCharMask << seq_encoder_.alphabet.size();
    static constexpr auto kExtension = DBGHashFast5::kExtension;
};

template <typename KMER>
DBGHashFast5Impl<KMER>::DBGHashFast5Impl(size_t k,
                                         bool canonical_mode,
                                         bool packed_serialization)
      : k_(k),
        canonical_mode_(canonical_mode),
        packed_serialization_(packed_serialization),
        kIgnoreLastCharMask(~(KmerPrefix((1llu << KMER::kBitsPerChar) - 1) << int(KMER::kBitsPerChar * (k - 1))))
//      { kmers_.reserve(50'000'000);
//        bits_.reserve(50'000'000);
//        next_node_.reserve(50'000'000);
//        next_char_.reserve(50'000'000);
//        may_contain_source_kmer_.reserve(50'000'000); };
//      { kmers_.reserve(50'000);
//        bits_.reserve(50'000);
//        next_node_.reserve(50'000);
//        next_char_.reserve(50'000);
//        may_contain_source_kmer_.reserve(50'000); };
    {};


template <typename KMER>
void DBGHashFast5Impl<KMER>::add_sequence(const std::string &sequence,
                                            bit_vector_dyn *nodes_inserted) {
    assert(!nodes_inserted || nodes_inserted->size() == num_nodes() + 1);

    auto kmers = sequence_to_kmers(sequence);
    KmerIterator iter = kmers_.end();
    KmerIterator next_kmer_prefix_it = kmers_.end();
    bool may_contain_source_kmer = true;
    bool next_iter = false;
    for (auto kmer_it = kmers.rbegin(); kmer_it != kmers.rend(); ++kmer_it) {
        const auto &kmer = *kmer_it;

        if (may_contain_source_kmer) { // (previous kmer, that is)
            next_kmer_prefix_it = kmers_.end();
            next_iter = false;
        }
        may_contain_source_kmer = kmer_it + 1 == kmers.rend() || !kmers_overlap(*(kmer_it + 1), kmer);

        Bits val = Bits(1) << kmer[k_ - 1];

        const auto &&key = KmerPrefix(kmer.data()) & kIgnoreLastCharMask;

        bool inserted;
        auto next_kmer_prefix_index = get_index(next_kmer_prefix_it);
        std::tie(iter, inserted) = kmers_.insert(key);
        if (inserted) {
            bits_.push_back(val);
            next_node_.push_back(next_iter ? next_kmer_prefix_index : npos);
            next_char_.push_back(next_iter ? seq_encoder_.decode(kmer[k_ - 1]) : 0);
            may_contain_source_kmer_.push_back(may_contain_source_kmer);
        }
        assert(iter != kmers_.end());
        assert(iter == find_kmer(key));

        if (!inserted) {
            const auto index = get_bit_index(iter);
            bits_[index] |= val;
            if (next_node_[index] == npos && next_iter) {
                next_node_[index] = get_index(next_kmer_prefix_it);
                next_char_[index] = seq_encoder_.decode(kmer[k_ - 1]);
            }
            if (may_contain_source_kmer)
                may_contain_source_kmer_[index] = may_contain_source_kmer;
        }

/*
        std::cout << "sequence: " << sequence << std::endl;
        std::cout << "kmer: " << kmer.to_string(k_, seq_encoder_.alphabet) << std::endl;
        for (auto it = kmers_.begin(); it != kmers_.end(); ++it) {
            const auto index = get_bit_index(it);
            std::cout << (uint64_t)(it - kmers_.begin()) << "," << get_index(it) << ":";
            std::cout << KMER(it.key()).to_string(k_ - 1, seq_encoder_.alphabet) << " ";
            std::cout << std::bitset<8>(bits_[index]) << " ";
            std::cout << next_node_[index] << " ";
            std::cout << next_char_[index] << " " << std::endl;
        }
        std::cout << "---" << std::endl;
*/


        if (iter != kmers_.end() && nodes_inserted)
            nodes_inserted->insert_bit(kmers_.size() - 1, true);

        next_kmer_prefix_it = iter;
        next_iter = true;
    }

    if (!canonical_mode_)
        return;
/*
    auto rev_kmers = sequence_to_kmers(seq_encoder_.reverse_complement(sequence));
    next_kmer_prefix_it = kmers_.end();
    for (auto kmer_it = rev_kmers.rbegin(); kmer_it != rev_kmers.rend(); ++kmer_it) {
        const auto &kmer = *kmer_it;

        if (kmer_it == kmers.rbegin() || !kmers_overlap(kmer, *(kmer_it - 1))) {
            const auto &next_kmer_prefix = KmerPrefix(kmer.data()) >> KMER::kBitsPerChar;
            next_kmer_prefix_it = find_kmer(next_kmer_prefix);

            if (next_kmer_prefix_it == kmers_.end()) {
                next_kmer_prefix_it = kmers_.insert(next_kmer_prefix).first; // dummy
                bits_.push_back(Bits(0));
            }
        }

        Bits incoming_val = (Bits(1) << kmer[0]) << seq_encoder_.alphabet.size();
        assert(next_kmer_prefix_it != kmers_.end());
        bits_[get_bit_index(next_kmer_prefix_it)] |= incoming_val;

        Bits val = Bits(1) << kmer[k_ - 1];

        const auto &&key = KmerPrefix(kmer.data()) & kIgnoreLastCharMask;

        auto &&[iter, inserted] = kmers_.insert(key);
        if (inserted)
            bits_.push_back(val);
        assert(iter != kmers_.end());
        assert(iter == find_kmer(key));

        if (!inserted)
            bits_[get_bit_index(iter)] |= val;

        if (iter != kmers_.end() && nodes_inserted)
            nodes_inserted->insert_bit(kmers_.size() - 1, true);

        next_kmer_prefix_it = iter;
    }
*/
}

// Traverse graph mapping sequence to the graph nodes
// and run callback for each node until the termination condition is satisfied.
// Guarantees that nodes are called in the same order as the input sequence.
// In canonical mode, non-canonical k-mers are NOT mapped to canonical ones
template <typename KMER>
void DBGHashFast5Impl<KMER>::map_to_nodes_sequentially(
                              std::string::const_iterator begin,
                              std::string::const_iterator end,
                              const std::function<void(node_index)> &callback,
                              const std::function<bool()> &terminate) const {
    std::string sequence(begin, end);

    const auto &kmers = sequence_to_kmers(sequence);
    auto it = kmers.begin();
    for (bool is_valid : seq_encoder_.valid_kmers(sequence, k_)) {

        assert(it != kmers.end() || !is_valid);

        if (terminate())
            return;

        assert((find_kmer(KmerPrefix((*it).data()) & kIgnoreLastCharMask) == kmers_.end())
               || get_index(*it) == npos
               || *it == get_kmer(get_index(*it), get_const_iter(get_index(*it))));

        callback(is_valid ? get_index(*it++) : npos);
    }
    assert(it == kmers.end());
}

// Traverse graph mapping sequence to the graph nodes
// and run callback for each node until the termination condition is satisfied
template <typename KMER>
void DBGHashFast5Impl<KMER>::map_to_nodes(const std::string &sequence,
                                            const std::function<void(node_index)> &callback,
                                            const std::function<bool()> &terminate) const {

    const auto &kmers = sequence_to_kmers(sequence, canonical_mode_);
    auto it = kmers.begin();
    for (bool is_valid : seq_encoder_.valid_kmers(sequence, k_)) {

        assert(it != kmers.end() || !is_valid);

        if (terminate())
            return;

        assert(!get_index(*it) || *it == get_kmer(get_index(*it), get_const_iter(get_index(*it))));

        callback(is_valid ? get_index(*it++) : npos);
    }
    assert(it == kmers.end());
}

template <typename KMER>
void DBGHashFast5Impl<KMER>::call_outgoing_kmers(node_index node,
                                                 const OutgoingEdgeCallback &callback) const {
    assert(in_graph(node));

    KMER kmer = get_kmer(node, get_const_iter(node));

    node_index next_kmer_base_index;
    KmerConstIterator next_kmer_prefix_it;

    if (seq_encoder_.decode(kmer[k_ - 1]) == next_char_[get_bit_index(get_const_iter(node))]) {
        next_kmer_base_index = next_node_[get_bit_index(get_const_iter(node))];
        next_kmer_prefix_it = get_const_iter(next_kmer_base_index);

        assert(next_kmer_prefix_it == find_kmer(KmerPrefix(kmer.data()) >> KMER::kBitsPerChar));
    } else {
        next_kmer_prefix_it = find_kmer(KmerPrefix(kmer.data()) >> KMER::kBitsPerChar);

        if (next_kmer_prefix_it == kmers_.end())
            return;

        next_kmer_base_index = get_index(next_kmer_prefix_it);
    }

    const Bits val = bits_[get_bit_index(next_kmer_prefix_it)];

    for (size_t i = 0; i < seq_encoder_.alphabet.size(); ++i) {
        if ((val >> i) & 1) {
            auto next_kmer_index = next_kmer_base_index + i;

            assert(in_graph(next_kmer_index));

            callback(next_kmer_index, seq_encoder_.decode(i));
        }
    }
}

template <typename KMER>
void DBGHashFast5Impl<KMER>::call_incoming_kmers(node_index node,
                                                 const IncomingEdgeCallback &callback) const {
    assert(in_graph(node));


    const auto &kmer = get_kmer(node, get_const_iter(node));

    for (char c : seq_encoder_.alphabet) {
        auto prev_kmer = kmer;
        prev_kmer.to_prev(k_, seq_encoder_.encode(c));

        auto prev = get_index(prev_kmer);
        if (prev != npos)
            callback(prev, c);
    }
    return;
}

template <typename KMER>
typename DBGHashFast5Impl<KMER>::node_index
DBGHashFast5Impl<KMER>::traverse(node_index node, char next_char) const {
    assert(in_graph(node));

    auto kmer = get_kmer(node, get_const_iter(node));
    kmer.to_next(k_, seq_encoder_.encode(next_char));
    return get_index(kmer);
}

template <typename KMER>
typename DBGHashFast5Impl<KMER>::node_index
DBGHashFast5Impl<KMER>::traverse_back(node_index node, char prev_char) const {
    assert(in_graph(node));

    auto kmer = get_kmer(node, get_const_iter(node));
    kmer.to_prev(k_, seq_encoder_.encode(prev_char));
    return get_index(kmer);
}

template <typename KMER>
void
DBGHashFast5Impl<KMER>
::adjacent_outgoing_nodes(node_index node,
                          const std::function<void(node_index)> &callback) const {
    assert(in_graph(node));

    call_outgoing_kmers(node, [&](auto child, char) { callback(child); });
}

template <typename KMER>
void
DBGHashFast5Impl<KMER>
::adjacent_incoming_nodes(node_index node,
                          const std::function<void(node_index)> &callback) const {
    assert(in_graph(node));

    call_incoming_kmers(node, [&](auto parent, char) { callback(parent); });
}

template <typename KMER>
size_t DBGHashFast5Impl<KMER>::outdegree(node_index node) const {
    assert(in_graph(node));

    KMER kmer = get_kmer(node, get_const_iter(node));
    //assert((*find_kmer(KmerPrefix(kmer.data()) & kIgnoreLastCharMask)).second
    //            && "val is not 0, there at least one k-mer");

    kmer.to_next(k_, 0);

    const auto next_kmer_prefix_it
        = find_kmer(KmerPrefix(kmer.data()) & kIgnoreLastCharMask);

    if (next_kmer_prefix_it == kmers_.end())
        return 0;

    const auto val = bits_[get_bit_index(next_kmer_prefix_it)] & kLastCharMask;

#if _PROTEIN_GRAPH
    return __builtin_popcountl(val);
#elif _DNA_CASE_SENSITIVE_GRAPH
    return __builtin_popcount(val);
#elif _DNA5_GRAPH
    return __builtin_popcount(val);
#elif _DNA_GRAPH
    return __builtin_popcount(val);
#else
    static_assert(false, "invalid or undefined alphabet");
#endif
}

template <typename KMER>
bool DBGHashFast5Impl<KMER>::has_single_outgoing(node_index node) const {
    assert(in_graph(node));

    return outdegree(node) == 1;
}

template <typename KMER>
bool DBGHashFast5Impl<KMER>::has_multiple_outgoing(node_index node) const {
    assert(in_graph(node));

    return outdegree(node) > 1;
}

template <typename KMER>
size_t DBGHashFast5Impl<KMER>::indegree(node_index node) const {
    assert(in_graph(node));

    size_t indegree = 0;
    call_incoming_kmers(node, [&](auto, auto) { indegree++; });
    return indegree;
}

template <typename KMER>
bool DBGHashFast5Impl<KMER>::has_no_incoming(node_index node) const {
    assert(in_graph(node));


    if (!may_contain_source_kmer_[get_const_iter(node) - kmers_.begin()])
        return false;

    return indegree(node) == 0;
}

template <typename KMER>
bool DBGHashFast5Impl<KMER>::has_single_incoming(node_index node) const {
    assert(in_graph(node));

    return indegree(node) == 1;
}

template <typename KMER>
typename DBGHashFast5Impl<KMER>::node_index
DBGHashFast5Impl<KMER>::kmer_to_node(const std::string &kmer) const {
    assert(kmer.length() == k_);

    return get_index(seq_encoder_.encode(kmer));
}

template <typename KMER>
std::string DBGHashFast5Impl<KMER>::get_node_sequence(node_index node) const {
    assert(in_graph(node));

    return seq_encoder_.kmer_to_sequence(get_kmer(node, get_const_iter(node)), k_);
}

class Serializer {
  public:
    explicit Serializer(std::ostream &os) : os_(os) {}

    template <class T>
    void operator()(const T &value) {
        os_.write(reinterpret_cast<const char *>(&value), sizeof(T));
    }

  private:
    std::ostream &os_;
};

class Deserializer {
  public:
    explicit Deserializer(std::istream &is) : is_(is) {}

    template <class T>
    T operator()() {
        T value;
        is_.read(reinterpret_cast<char *>(&value), sizeof(T));
        return value;
    }

  private:
    std::istream &is_;
};

template <typename KMER>
void DBGHashFast5Impl<KMER>::serialize(std::ostream &out) const {
    if (!out.good())
        throw std::ofstream::failure("Error: trying to dump graph to a bad stream");

    out.exceptions(out.badbit | out.failbit);

    serialize_number(out, k_);

    Serializer serializer(out);

    if (packed_serialization_) {
        serialize_number(out, kmers_.size());
        std::for_each(kmers_.begin(), kmers_.end(), serializer);
        serialize_number(out, bits_.size());
        std::for_each(bits_.begin(), bits_.end(), serializer);
        serialize_number(out, next_node_.size());
        std::for_each(next_node_.begin(), next_node_.end(), serializer);
        serialize_number(out, next_char_.size());
        std::for_each(next_char_.begin(), next_char_.end(), serializer);
        serialize_number(out, may_contain_source_kmer_.size());
        std::for_each(may_contain_source_kmer_.begin(), may_contain_source_kmer_.end(), serializer);
    } else {
        throw std::runtime_error("not implemented");
        //serialize_number(out, std::numeric_limits<uint64_t>::max());
        //kmers_.serialize(serializer);
    }

    serialize_number(out, canonical_mode_);
}

template <typename KMER>
void DBGHashFast5Impl<KMER>::serialize(const std::string &filename) const {
    std::ofstream out(utils::remove_suffix(filename, kExtension) + kExtension,
                      std::ios::binary);
    serialize(out);
}

template <typename KMER>
bool DBGHashFast5Impl<KMER>::load(std::istream &in) {
    if (!in.good())
        return false;

    in.exceptions(in.badbit | in.failbit | in.eofbit);

    kmers_.clear();
    bits_.clear();
    may_contain_source_kmer_.clear();

    try {
        k_ = load_number(in);

        Deserializer deserializer(in);

        uint64_t tag = load_number(in);

        if (tag < std::numeric_limits<uint64_t>::max()) {
            packed_serialization_ = true;

            const auto size = tag;
            kmers_.reserve(size + 1);
            for (uint64_t i = 0; i < size; ++i) {
                kmers_.insert(deserializer.operator()<KmerPrefix>());
            }

            uint64_t size2 = load_number(in);
            bits_.reserve(size2 + 1);
            for (uint64_t i = 0; i < size2; ++i) {
                bits_.push_back(deserializer.operator()<Bits>());
            }

            uint64_t size3 = load_number(in);
            next_node_.reserve(size3 + 1);
            for (uint64_t i = 0; i < size3; ++i) {
                next_node_.push_back(deserializer.operator()<node_index>());
            }

            uint64_t size4 = load_number(in);
            next_char_.reserve(size4 + 1);
            for (uint64_t i = 0; i < size4; ++i) {
                next_char_.push_back(deserializer.operator()<char>());
            }

            uint64_t size5 = load_number(in);
            may_contain_source_kmer_.reserve(size5 + 1);
            for (uint64_t i = 0; i < size5; ++i) {
                may_contain_source_kmer_.push_back(deserializer.operator()<bool>());
            }

        } else {
            throw std::runtime_error("not implemented");
            //packed_serialization_ = false;
            //kmers_ = KmerIndex::deserialize(deserializer, true);
        }

        canonical_mode_ = load_number(in);

        return in.good();

    } catch (...) {
        return false;
    }
}

template <typename KMER>
bool DBGHashFast5Impl<KMER>::load(const std::string &filename) {
    std::ifstream in(utils::remove_suffix(filename, kExtension) + kExtension,
                     std::ios::binary);
    return load(in);
}

template <typename KMER>
bool DBGHashFast5Impl<KMER>::operator==(const DeBruijnGraph &other) const {
    if (get_k() != other.get_k()
            || is_canonical_mode() != other.is_canonical_mode()
            || num_nodes() != other.num_nodes())
        return false;

    if (!dynamic_cast<const DBGHashFast5Impl*>(&other))
        throw std::runtime_error("Not implemented");

    const auto &other_hash = *dynamic_cast<const DBGHashFast5Impl*>(&other);

    if (this == &other_hash)
        return true;

    assert(k_ == other_hash.k_);
    assert(canonical_mode_ == other_hash.canonical_mode_);
    assert(kmers_.size() == other_hash.kmers_.size());

    return kmers_ == other_hash.kmers_;
}

template <typename KMER>
typename DBGHashFast5Impl<KMER>::node_index
DBGHashFast5Impl<KMER>::get_index(const KmerConstIterator &kmer_iter) const {
    return ((kmer_iter - kmers_.begin()) << KMER::kBitsPerChar) + 1;
}

template <typename KMER>
typename DBGHashFast5Impl<KMER>::node_index
DBGHashFast5Impl<KMER>::get_index(const Kmer &kmer) const {
    const auto find = find_kmer(KmerPrefix(kmer.data()) & kIgnoreLastCharMask);
    if (find == kmers_.end())
        return npos;

    if (!(Bits(1) & (bits_[get_bit_index(find)] >> kmer[k_ - 1])))
        return npos;

    node_index node = get_index(find) + kmer[k_ - 1];
    assert(get_kmer(node, find) == kmer);
    return node;
}

template <typename KMER>
typename DBGHashFast5Impl<KMER>::KmerIterator
DBGHashFast5Impl<KMER>::get_iter(node_index node) {
    KmerIterator iter = (kmers_.begin() + ((node - 1) >> KMER::kBitsPerChar));
    return iter;
}

template <typename KMER>
typename DBGHashFast5Impl<KMER>::KmerConstIterator
DBGHashFast5Impl<KMER>::get_const_iter(node_index node) const {
    KmerConstIterator iter = (kmers_.begin() + ((node - 1) >> KMER::kBitsPerChar));
    return iter;
}

template <typename KMER>
const KMER DBGHashFast5Impl<KMER>
::get_kmer(node_index node,
           KmerConstIterator kmer_iter) const {
    assert(in_graph(node));

    KMER prefix = KMER(*kmer_iter);

    KmerPrefix c = (node - 1) & KmerPrefix((1llu << KMER::kBitsPerChar) - 1);

    return KMER(KmerPrefix(prefix.data()) | (c << static_cast<int>(KMER::kBitsPerChar * (k_ - 1))));
}

template <typename KMER>
bool DBGHashFast5Impl<KMER>::in_graph(node_index node) const {
    assert(node > 0 && node <= num_nodes());

    auto it = get_const_iter(node);
    KmerPrefix c = (node - 1) & KmerPrefix((1llu << KMER::kBitsPerChar) - 1);
    return Bits(1) & (bits_[get_bit_index(it)] >> c);
}

template <typename KMER>
void DBGHashFast5Impl<KMER>::call_nodes(const std::function<void(node_index)> &callback,
                               const std::function<bool()> &stop_early) const {
    for (auto iter = kmers_.begin(); iter != kmers_.end() && !stop_early(); ++iter) {
        const auto val = bits_[get_bit_index(iter)];
        for (size_t i = 0; i < seq_encoder_.alphabet.size(); ++i) {
            if (val & (Bits(1) << i)) {
                callback(get_index(iter) + i);
            }
        }
    }
}

std::unique_ptr<DBGHashFast5::DBGHashFast5Interface>
DBGHashFast5::initialize_graph(size_t k,
                                 bool canonical_mode,
                                 bool packed_serialization) {
    if (k * KmerExtractor2Bit::bits_per_char <= 64) {
        return std::make_unique<DBGHashFast5Impl<KmerExtractor2Bit::Kmer64>>(
            k, canonical_mode, packed_serialization
        );
    } else if (k * KmerExtractor2Bit::bits_per_char <= 128) {
        return std::make_unique<DBGHashFast5Impl<KmerExtractor2Bit::Kmer128>>(
            k, canonical_mode, packed_serialization
        );
    } else {
        return std::make_unique<DBGHashFast5Impl<KmerExtractor2Bit::Kmer256>>(
            k, canonical_mode, packed_serialization
        );
    }
}

DBGHashFast5::DBGHashFast5(size_t k,
                               bool canonical_mode,
                               bool packed_serialization) {
    hash_dbg_ = initialize_graph(k, canonical_mode, packed_serialization);
}

bool DBGHashFast5::load(std::istream &in) {
    if (!in.good())
        return false;

    try {
        auto pos = in.tellg();
        auto k = load_number(in);
        in.seekg(pos, in.beg);

        // the actual value of |canonical| will be set in load
        hash_dbg_ = initialize_graph(k, false, false);
        return hash_dbg_->load(in) && in.good();
    } catch (...) {
        return false;
    }
}

bool DBGHashFast5::load(const std::string &filename) {
    std::ifstream in(utils::remove_suffix(filename, kExtension) + kExtension,
                     std::ios::binary);
    return load(in);
}