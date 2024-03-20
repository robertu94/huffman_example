#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fmt/ranges.h>
#include <memory>
#include <queue>
#include <random>
#include <unordered_map>
#include <vector>
#include <ranges>

int
main()
{
  using value_t = int32_t;
  using count_t = size_t;
  using id_t = size_t;
  constexpr size_t N = 30000000;

  std::vector<value_t> values(N);
  // setup: generate  random values  {{{
  std::seed_seq seed{};
  std::mt19937_64 gen{ seed };
  std::normal_distribution<float> dist(0, 1);
  auto rnd = [&] -> int32_t {
      return std::round(dist(gen));
  };
  std::generate(values.begin(), values.end(), rnd);
  //}}}
  // stage 1: histogram O(N) {{{
  //  we wouldn't' necessarily implement it like this (especially on GPU)
  //  we also don't plan on implementing this on device either.
  std::unordered_map<value_t, count_t> histogram;
  for (auto const& v : values) {
    histogram[v]++;
  }
  fmt::println("hist={}", histogram);
  //}}}
  // stage 2: convert the histogram into a huffman tree {{{
  // first some data structures and helper functions {{{
  struct queue_entry
  {
    id_t id = 0;       /// used to identify  interior nodes uniquely
    value_t value = 0; /// used to provide the values for leaf nodes
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
  std::unordered_map<value_t, std::vector<bool>> encoding;
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

  auto print = [](std::shared_ptr<tree> const& node) {
    auto print_impl = [](std::shared_ptr<tree> const& node,
                                 auto& print_ref) -> void {
      fmt::print("(");
      if (node->left) {
        print_ref(node->left, print_ref);
      }
      fmt::print(",");
      if(node->entry.id == 0) {
          fmt::print("{}", node->entry.value);
      }
      fmt::print(",");
      if (node->right) {
        print_ref(node->right, print_ref);
      };
      fmt::print(")");
    };
    return print_impl(node, print_impl);
  };
  (void)print;
  //print(root);
  fmt::println("encoding={}", encoding);
  ///}}}
  // stage 4: (serial): encode O(N) amortized time {{{
  auto serial_begin = std::chrono::steady_clock::now();
  std::vector<uint8_t> encoded;
  for (auto v : values) {
    // fmt::println("s={} -> e={}", v, encoding[v]);
    encoded.insert(encoded.end(), encoding.at(v).begin(), encoding.at(v).end());
  }
  auto serial_end = std::chrono::steady_clock::now();
  fmt::println(
    "serial={}",
    std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
      serial_end - serial_begin)
      .count());
  // fmt::println("    encoded={}", encoded);
  // }}}
  // alternative stage 4: (parallel) {{{
  // 1. compute the largest possible encoding size and preallocate encoded memory
  size_t largest_encoding = 0;
  for (auto const& [key, encoded] : encoding) {
      (void)key;
      largest_encoding = std::max(largest_encoding, encoded.size());
  }
  std::vector<uint8_t> par_encoded(values.size() * largest_encoding); //I'd use vector<bool> but it's not threadsafe

  //2. We'll need auxiliary storage for the offsets to write to, acc will store the final size
  //   NOTE, if we know that the array will fit in 4GB of memory, we should use uint32_t here
  //         since most CPU/GPU platforms only implement upto 64 bit atomic operations
  //         there are optimizations that we are required to omit here.
  size_t acc = 0;
  std::vector<size_t> offsets(values.size());

  auto par_begin = std::chrono::steady_clock::now();
  //3. in OpenMP, we prefer to launch of CPU threads only once the '{' specify their lifetime
  #pragma omp parallel
  {

    // 4. we do a exclusive scan to compute the offsets
    //
    //   for the sequence 1,2,3,4, the exclusive plus scan is 0,1,3,6
    //
    //   There are many parallel algorithms that implement this,
    //   but it is likely a device primitive that you can use from a standard
    //   library If not, this paper lists many implementations that work well on the
    //   GPU, but the principles are the same on CPU and GPU
    //
    //   Duane Merill, Micheal Garland. "Single-pass Parallel Prefix Scan with
    //   Decoupled Lookback". Nvidia Technical Report NVR-2016-002. March 2016
    //
    //
    #pragma omp for reduction(inscan, + : acc)
    for (size_t i = 0; i < offsets.size(); ++i) {
      offsets[i] = acc;
      #pragma omp scan exclusive(acc)
      acc += encoding.at(values[i]).size();
    }

    // OpenMP has an implicit barrier here that is technically unneeded if we can guarantee that
    // offsets[i] becomes valid in both loops at the same time.  We could do this by combining the loops
    // however, in this case splitting the loops is much faster because of better memory access patterns
    // and openmp requires we have a wait since they aren't the same loop

    //5. now that we know the offsets, we copy the bits the specified offsets for each encoded symbol
    //   we use nowait here to omit a duplicate synchronization call at the end of omp parallel and omp for
    #pragma omp for nowait
    for (size_t i = 0; i < offsets.size(); ++i) {
      auto encoded = encoding.at(values[i]);
      for (size_t j = 0; j < encoded.size(); ++j) {
        par_encoded[j + offsets[i]] = encoded[j];
      }
    }
  } // this is where omp parallel ends, and the threads die

  //6. at this point we could "free" all memory that we didn't use and copy into final storage.
  //   as an implemntation detail in libstdc++ and libc++, resize here just updates the size pointer,
  //   it does not actually free the storage.
  par_encoded.resize(acc);

  auto par_end = std::chrono::steady_clock::now();
  fmt::println(
    "par={}",
    std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
      par_end - par_begin)
      .count());
  // fmt::println("par_encoded={}", par_encoded);
  // }}}
}

// vim:foldmethod=marker:
