#include "systemproperties.h"
#include <QCoreApplication>
#include <cstdio>
#include <cerrno>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    check(parseOsName("PRETTY_NAME=\"Example Linux 2\"\n").value == "Example Linux 2", "Double quoted OS caption");
    check(parseOsName("PRETTY_NAME='Example $literal'\n").value == "Example $literal", "Single quotes preserve literal dollars");
    check(parseOsName("PRETTY_NAME=\"Escaped \\\"quote\\\" and \\$dollar\"\n").value == "Escaped \"quote\" and $dollar",
          "Decode permitted shell escapes without evaluation");
    check(parseOsName("PRETTY_NAME=\"$(must-not-run)\"\n").value == "$(must-not-run)", "Command substitution remains literal text");
    check(parseOsName("PRETTY_NAME=\"unterminated\n").state == ReadState::Error, "Malformed OS quote is explicit error");
    check(parseOsName("NAME=Example\n").value == "Linux", "Missing PRETTY_NAME uses documented Linux default");
    check(parseOsName("PRETTY_NAME=First\nPRETTY_NAME=Second\n").value == "Second", "Last OS definition wins");
    QSet<int> cpus;
    check(parseCpuList("0-3,8,10-11", cpus) && cpus.size() == 7 && cpus.contains(11) && !cpus.contains(9),
          "Sparse CPU ranges retain actual IDs");
    const QSet<int> previous = cpus;
    check(!parseCpuList("3-1", cpus) && cpus == previous, "Reversed ranges fail without partial output");
    check(!parseCpuList("0-500000", cpus), "CPU list expansion is bounded");
    check(!parseCpuList("0,", cpus) && !parseCpuList("-1", cpus) && !parseCpuList("word", cpus),
          "Invalid CPU identifiers never become CPU zero");
    QHash<QString, QString> memory {
        {"MEMORY_ARRAY_NUM_DEVICES", "2"},
        {"MEMORY_DEVICE_0_SIZE", "17179869184"},
        {"MEMORY_DEVICE_1_SIZE", "17179869184"}
    };
    check(parseInstalledMemory(memory).value == "34359738368", "Two 16 GiB devices report 32 GiB installed");
    memory["MEMORY_ARRAY_NUM_DEVICES"] = "3";
    memory["MEMORY_DEVICE_2_PRESENT"] = "0";
    check(parseInstalledMemory(memory).value == "34359738368", "Explicitly empty slot does not invalidate total");
    memory.remove("MEMORY_DEVICE_2_PRESENT");
    check(parseInstalledMemory(memory).state == ReadState::Unavailable, "Unknown slot never produces a partial total");
    memory["MEMORY_DEVICE_2_SIZE"] = "garbage";
    check(parseInstalledMemory(memory).state == ReadState::Error, "Malformed capacity is a read error");
    memory["MEMORY_DEVICE_2_SIZE"] = "18446744073709551615";
    check(parseInstalledMemory(memory).error == EOVERFLOW, "Installed capacity sum checks overflow");
    memory["MEMORY_DEVICE_2_PRESENT"] = "0";
    check(parseInstalledMemory(memory).state == ReadState::Error, "Nonzero size conflicts with empty slot evidence");
    memory.remove("MEMORY_DEVICE_2_SIZE");
    memory["MEMORY_DEVICE_0_NON_VOLATILE_SIZE"] = "0";
    memory["MEMORY_DEVICE_1_NON_VOLATILE_SIZE"] = "Unknown";
    check(parseInstalledMemory(memory).value == "34359738368", "Unknown optional nonvolatile field preserves known capacities");
    memory["MEMORY_DEVICE_0_NON_VOLATILE_SIZE"] = "17179869184";
    check(parseInstalledMemory(memory).state == ReadState::Unsupported, "Persistent capacity is not silently counted as RAM");
    memory["MEMORY_ARRAY_NUM_DEVICES"] = "4097";
    check(parseInstalledMemory(memory).error == EOVERFLOW, "Memory slot count is bounded");
    check(parseInstalledMemory({}).state == ReadState::Unavailable, "Missing firmware data is not zero RAM");
    return failures ? 1 : 0;
}
