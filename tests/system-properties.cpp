#include "systemproperties.h"
#include <QCoreApplication>
#include <cstdio>

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
    return failures ? 1 : 0;
}
