#include <queue>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <numeric>
#include <stdexcept>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

template<class... Ts>
struct overloads : Ts... { using Ts::operator()...; };

namespace huffman {
    using value_t = int32_t;
    using count_t = size_t;
    using id_t = size_t;
    using index_t = size_t;
    using encoded_t = std::vector<bool>;

    template <class Value>
    auto build_encoding(std::map<Value, count_t> const& histogram) {
        if(histogram.size() <= 1) throw std::invalid_argument("at least 2 symbols are required");
        struct queue_entry
        {
            id_t id = 0;       /// used to identify  interior nodes uniquely
            Value value = 0; /// used to provide the values for leaf nodes
            count_t count = 0; /// used to provide the count of interior or leaf nodes
            bool operator>(queue_entry const& rhs) const
            {
                // comparisons will be based on count
                return count > rhs.count;
            }
        };

        // now we need a structure to hold the tree itself
        // the tree implementation in std::map isn't good enough because we need
        // explicit left/right pointers
        struct tree
        {
            queue_entry entry{};
            std::shared_ptr<tree> left{};
            std::shared_ptr<tree> right{};
        };

        // while we are building up the tree, we may need to be able to reference
        // an existing interior tree node until it is connected as the child of
        // another node, we use interiors to track this
        std::unordered_map<id_t, std::shared_ptr<tree>> interiors;

        // when inserting children to the huffman tree, we'll either use an existing
        // interior child or insert a leaf node
        auto insert_or_retrieve_child = [&interiors](std::shared_ptr<tree>& child,
                queue_entry const& entry) {
            if (interiors.contains(entry.id)) {
                child = interiors.at(entry.id);
                interiors.erase(entry.id);
            } else {
                child = std::make_shared<tree>(
                        tree{ .entry = { .value = entry.value, .count = entry.count } });
            }
        };
        //}}}
        //We want a min priority queue
std::priority_queue<queue_entry,
    std::vector<queue_entry>,
    std::greater<queue_entry>>
    Q;

    // consider the symbols from least popular to most popular using the min queue
    for (auto const& [value, count] : histogram) {
        Q.emplace(queue_entry{ .id = 0, .value = value, .count = count });
    }

// interior nodes have an ID starting at 1, all leaf nodes have an id of 0
id_t max_id = 1;

while (Q.size() >= 2) {
    //considering nodes 2 at a time
    //1. create a new parent interior node, and update the max_id
    auto new_node = interiors[max_id] =
        std::make_shared<tree>(tree{ .entry{ .id = max_id } });
    max_id++;
    //2. set it's left and right child to be the nodes with the least popularity
    insert_or_retrieve_child(new_node->left, Q.top());
    Q.pop();
    insert_or_retrieve_child(new_node->right, Q.top());
    Q.pop();
    //3. set the count on this interior node
    new_node->entry.count =
        new_node->left->entry.count + new_node->right->entry.count;
    //4. insert this new interior node into the min-queue
    Q.emplace(new_node->entry);
}
//}}}
// stage 3: compute the node labels recursively {{{
std::unordered_map<Value, std::vector<bool>> encoding;
auto walk = [&encoding](std::shared_ptr<tree> const& node) {
    auto walk_impl = [&encoding](std::shared_ptr<tree> const& node,
            std::vector<bool> const& path,
            auto& walk_ref) -> void {
        // 2. if we have a left child, visit the left path adding a zero bit to the encoding
        // and recurse
        if (node->left) {
            std::vector left_path = path;
            left_path.emplace_back(false);
            walk_ref(node->left, left_path, walk_ref);
        }
        // 3. if we have a right child, visit the right path adding a one bit to the encoding
        // and recurse
        if (node->right) {
            std::vector right_path = path;
            right_path.emplace_back(true);
            walk_ref(node->right, right_path, walk_ref);
        };
        // 4. if we are at a leaf node, record the encoding
        if (node->entry.id == 0) {
            encoding[node->entry.value] = path;
        }
    };
    return walk_impl(node, {}, walk_impl);
};
  // 1. start at the root of the tree
  auto root = interiors.at(max_id-1);
  walk(root);
  return encoding;
}
}

int main() {
    using namespace huffman;
    std::map<value_t, count_t> hist{{5, 95},      {-5, 106},    {-4, 7022},
        {4, 6938},    {3, 179139},  {-2, 1819395},
        {1, 7253819}, {-3, 179326}, {0, 11488681},
        {2, 1817008}, {-1, 7248471}};

    // Step 1: Convert the histogram into a fixed length huffman tree

    // see from https://dl.acm.org/doi/pdf/10.1145/79147.79150 for an optimal
    // solution in O(nL) time. where n is the number of elements in the alphabet,
    // and L is the length of the longest Huffman encoding in bits

    // for now we implement a naive O(n^2 log n) solution where we combine the
    // least probable elements together until we achieve a desired max encoding
    // length.  This version may take more bits than required
    const size_t allowed_length = 8;
    std::unordered_map<std::optional<value_t>, encoded_t> encoding;
    std::vector<value_t> symbols_least_to_most_probable;
    std::transform(hist.begin(), hist.end(),
                   std::back_inserter(symbols_least_to_most_probable),
                   [](auto const &x) { return x.first; });
    std::sort(symbols_least_to_most_probable.begin(), symbols_least_to_most_probable.end(),
              [&](auto const& lhs, auto const& rhs){ return hist[lhs] < hist[rhs];});

    // we encode the unpredictable symbol as an empty optional
    std::map<std::optional<value_t>, count_t> working_hist(hist.begin(), hist.end());
    for(auto least_symbol : symbols_least_to_most_probable) {
        try {
            working_hist[std::nullopt] += hist[least_symbol];
            working_hist.erase(least_symbol);
            encoding = build_encoding(working_hist);
            using entry_t = typename decltype(encoding)::value_type;
            size_t max_length = std::reduce(encoding.begin(), encoding.end(),
                    size_t{0}, 
                    overloads{
                        [](size_t l, size_t r){return std::max(l,r);},
                        [](size_t l, entry_t r){return std::max(l,r.second.size());},
                        [](entry_t l, size_t r){return std::max(l.second.size(),r);},
                        [](entry_t l, entry_t r){return std::max(l.second.size(),r.second.size());},
                    });
            fmt::println("maxl={}, working_hist={}, encoding={}", max_length, working_hist, encoding);
            if(max_length <= allowed_length) {
                break;
            }
        } catch(std::invalid_argument const& ex) {
            throw std::runtime_error("cannot solve encoding");
        }
    }

    // Step 2: Now that we have a tree that fits in given number of bits, we can encode it into an array
    value_t least_non_null_value =
        std::max_element(encoding.begin(), encoding.end(), 
                [](auto lhs, auto rhs){
                    if(!lhs.first && !rhs.first) return false;
                    else if(lhs.first && !rhs.first) return false;
                    else if(!lhs.first && rhs.first) return true;
                    else return lhs.first > rhs.first;
                })->first.value();
    value_t greatest_non_null_value =
        std::max_element(encoding.begin(), encoding.end(), 
                [](auto lhs, auto rhs){
                    if(!lhs.first && !rhs.first) return false;
                    else if(lhs.first && !rhs.first) return false;
                    else if(!lhs.first && rhs.first) return true;
                    else return lhs.first < rhs.first;
                })->first.value();
    fmt::println("lnnv={} gnnv={}", least_non_null_value, greatest_non_null_value);

    std::vector<std::vector<bool>> encoding_table(greatest_non_null_value - least_non_null_value);
    for(size_t i = 0; i < encoding_table.size(); ++i) {
        if(encoding.contains(i+least_non_null_value)) {
            encoding_table[i] = encoding[i+least_non_null_value];
        } else {
            encoding_table[i] = encoding[std::nullopt];
        }
    }
    
    std::vector<value_t> sequence {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5};
    std::vector<bool> encoded_sequence;
    std::vector<value_t> unpredictable;
    auto const unpredictable_encoding = encoding[std::nullopt];

    for(auto i :sequence) {
        auto encode_index = i - least_non_null_value;
        if (encode_index < 0 || encode_index >= static_cast<int>(encoding_table.size()) || encoding_table[encode_index] == unpredictable_encoding)  {
            fmt::println("{} -> U:{}", i, unpredictable_encoding);
            encoded_sequence.insert(encoded_sequence.end(), unpredictable_encoding.begin(), unpredictable_encoding.end());
            unpredictable.emplace_back(i);
        } else {
            fmt::println("{} -> {}", i, encoding_table[encode_index]);
            encoded_sequence.insert(encoded_sequence.end(), encoding_table[encode_index].begin(), encoding_table[encode_index].end());
        }
     }

    std::vector<value_t> decoded_sequence;
    std::vector<bool> so_far;
    std::unordered_map<std::vector<bool>, int> encodings_to_indices;
    size_t unpred_idx = 0;
    for(size_t i= 0; i < encoding_table.size(); ++i) {
        encodings_to_indices[encoding_table[i]] = i;
    }
    encodings_to_indices[unpredictable_encoding] = 0; // this will never be read so the value doesn't matter
    for(size_t i= 0; i < encoded_sequence.size(); ++i) {
        so_far.push_back(encoded_sequence[i]);
        if(encodings_to_indices.contains(so_far)) {
            if(so_far != unpredictable_encoding) {
                fmt::println("decoding {} -> {}", so_far, encodings_to_indices[so_far]+least_non_null_value);
                decoded_sequence.emplace_back(encodings_to_indices[so_far]+least_non_null_value);
            } else {
                fmt::println("decoding {} -> U:{}", so_far, unpredictable[unpred_idx]);
                decoded_sequence.emplace_back(unpredictable[unpred_idx++]);
            }
            so_far.clear();
        }
    }
    fmt::println("decoded={}", decoded_sequence);
    
    
    

    



}
