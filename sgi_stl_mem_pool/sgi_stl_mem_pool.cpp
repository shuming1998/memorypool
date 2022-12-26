#include "sgi_stl_mem_pool.hpp"
#include <vector>
#include <iostream>
using namespace std;
using namespace sgi_stl;

int main() {

  vector<string, Allocator<string>> vec;
  for (int i = 0; i < 100; ++i) {
    vec.emplace_back(to_string(rand() % 100));
  }

  for (const string n : vec) {
    cout << n << '\n';
  }

  return 0;
}