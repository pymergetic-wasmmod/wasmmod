// Syntax-highlight sample for the package viewer (not linked into the pack).
// Avoid system headers so editors without a C++ sysroot stay quiet.

namespace hello {

inline int add(int a, int b) { return a + b; }

struct Point {
  int x;
  int y;
};

}  // namespace hello

int main() {
  hello::Point p{2, 3};
  return hello::add(p.x, p.y);
}
