#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <sys/mman.h>

#include "../../runtime/FunctionHeader.h"
#include "../../runtime/Context.h"

void panic() { std::abort(); }

static void check(bool ok, const char* message) {
    if(!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

#if defined(__linux__) && defined(__x86_64__)
static std::atomic<FunctionHeader*> expectedHeader{nullptr};
static std::atomic<void*> trapDestination{nullptr};
static volatile sig_atomic_t traps = 0;
static volatile sig_atomic_t invalidTrap = 0;

static void onTestTrap(int, siginfo_t*, void* saved) {
    Context context(saved);
    void* address = static_cast<unsigned char*>(context.ip()) - Trap::TrapAdjust;
    if(FunctionHeader::fromTrapAddress(address) != expectedHeader.load()) invalidTrap = 1;
    traps = traps + 1;
    context.ip() = trapDestination.load();
}
#endif

int main() {
#if defined(__linux__) && defined(__x86_64__)
    // Reserve sparse address space, but commit only two pages. The destinations
    // differ in BOTH 32-bit halves: mixing halves cannot accidentally yield
    // either valid target. Both code copies
    // stay live until all readers join. This is not a reclamation test.
    const size_t farOffset = (size_t(1) << 32) + 4096;
    const size_t mappingSize = farOffset + 4096;
    auto* memory = static_cast<unsigned char*>(mmap(nullptr, mappingSize,
        PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    check(memory != MAP_FAILED, "reserve sparse fixture");
    check(mprotect(memory, 4096, PROT_READ | PROT_WRITE | PROT_EXEC) == 0,
        "commit entry and first target page");
    check(mprotect(memory + farOffset, 4096,
        PROT_READ | PROT_WRITE | PROT_EXEC) == 0, "commit second target page");
    auto* targetA = memory + 64;
    auto* targetB = memory + farOffset + 80;
    check(uint32_t(uintptr_t(targetA)) != uint32_t(uintptr_t(targetB)) &&
        (uintptr_t(targetA) >> 32) != (uintptr_t(targetB) >> 32),
        "target addresses differ in both halves");
    const unsigned char first[] = {0xb8, 11, 0, 0, 0, 0xc3};
    const unsigned char second[] = {0xb8, 22, 0, 0, 0, 0xc3};
    std::memcpy(targetA, first, sizeof(first));
    std::memcpy(targetB, second, sizeof(second));
    auto* header = new(memory) FunctionHeader(nullptr);
    const unsigned char expected[] = {0xff, 0x25, 2, 0, 0, 0, 0x90, 0x90};
    check(std::memcmp(memory, expected, sizeof(expected)) == 0,
        "immutable RIP-relative indirect jump encoding");
    check(header->trapAddress() == memory + 16 && memory[16] == 0xcc,
        "trap location and instruction");
    check(FunctionHeader::fromTrapAddress(memory + 16) == header,
        "trap address recovers the header");
    check(header->getFunction() == nullptr, "metadata round trip");
    using Entry = int (*)();
    auto entry = reinterpret_cast<Entry>(memory);
    expectedHeader = header;
    trapDestination = targetA;
    struct sigaction action = {};
    struct sigaction previous = {};
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = onTestTrap;
    action.sa_flags = SA_SIGINFO;
    check(sigaction(SIGTRAP, &action, &previous) == 0, "install trap handler");
    check(entry() == 11 && traps == 1 && !invalidTrap, "initial trap delivery");
    header->jumpTo(targetA);
    unsigned char instructions[8];
    std::memcpy(instructions, memory, sizeof(instructions));
    header->jumpTo(targetB);
    check(std::memcmp(instructions, memory, sizeof(instructions)) == 0,
        "publication must not rewrite entry instructions");
    header->trap();
    check(std::memcmp(instructions, memory, sizeof(instructions)) == 0,
        "arming a trap must not rewrite entry instructions");
    check(entry() == 11 && traps == 2 && !invalidTrap, "re-armed trap delivery");
    // A CPU can acquire the trap target before the owner disarms it, and only
    // reach the trap afterwards. Its address/metadata must remain valid.
    auto staleTrap = reinterpret_cast<Entry>(header->trapAddress());
    header->jumpTo(targetB);
    check(staleTrap() == 11 && traps == 3 && !invalidTrap,
        "previously acquired trap remains valid after redirection");
    check(sigaction(SIGTRAP, &previous, nullptr) == 0, "restore trap handler");

    header->jumpTo(targetA);
    check(entry() == 11, "first destination");
    header->jumpTo(targetB);
    check(entry() == 22, "second destination");

    std::atomic<bool> start{false};
    std::atomic<unsigned> ready{0};
    std::atomic<unsigned> finished{0};
    std::atomic<unsigned> invalid{0};
    std::vector<std::thread> readers;
    for(unsigned reader = 0; reader < 4; ++reader) {
        readers.emplace_back([&] {
            ++ready;
            while(!start.load(std::memory_order_acquire)) {}
            for(unsigned i = 0; i < 100000; ++i) {
                int result = entry();
                if(result != 11 && result != 22) ++invalid;
            }
            ++finished;
        });
    }
    while(ready.load() != 4) {}
    start.store(true, std::memory_order_release);
    unsigned long updates = 0;
    while(updates < 100000 || finished.load() != 4) {
        header->jumpTo(updates % 2 ? targetA : targetB);
        ++updates;
    }
    for(auto& reader : readers) reader.join();
    check(invalid.load() == 0, "concurrent callers see complete destinations");
    check(std::memcmp(instructions, memory, sizeof(instructions)) == 0,
        "instructions remain unchanged after concurrent publication");
    check(memory[16] == 0xcc && header->getFunction() == nullptr,
        "trap instruction and metadata remain unchanged");
    header->~FunctionHeader();
    check(munmap(memory, mappingSize) == 0, "unmap fixture after readers stop");
    std::printf("entry publication passed (400000 calls, %lu updates)\n", updates);
#else
    std::puts("entry publication requires Linux x86_64");
#endif
}
