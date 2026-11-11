// Electric-fence style global operator new, for the damaged-input run when the ASan runtime is not installed:
// every heap block ends right at a PROT_NONE page, so reading or writing past the end of any block faults.
#include <sys/mman.h>
#include <cstddef>
#include <cstdint>
#include <new>

namespace {
constexpr size_t kPage = 4096;
}

void* operator new(size_t n) {
    if (n == 0) n = 1;
    size_t body = (n + kPage - 1) / kPage * kPage;
    size_t total = kPage + body + kPage;  // header page (holds `total`), body, guard page
    void* m = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) throw std::bad_alloc();
    char* base = static_cast<char*>(m);
    *reinterpret_cast<size_t*>(base) = total;
    mprotect(base + kPage + body, kPage, PROT_NONE);
    return base + kPage + body - n;  // block ends at the guard page
}

void operator delete(void* p) noexcept {
    if (!p) return;
    char* base = reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(p) / kPage * kPage) - kPage;
    munmap(base, *reinterpret_cast<size_t*>(base));
}
void operator delete(void* p, size_t) noexcept { operator delete(p); }
