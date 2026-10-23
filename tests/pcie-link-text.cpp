#include "pcielinktext.h"
#include <QCoreApplication>
#include <limits>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QLocale::setDefault(QLocale::c());
    int failures = 0;
    const auto check = [&](bool ok, const char *why) {
        if (!ok) { std::fprintf(stderr, "%s\n", why); ++failures; }
    };
    const double rates[] {2.5, 5, 8, 16, 32, 64, 128};
    int generation = 0;
    for (double rate : rates) {
        ++generation;
        check(pcieLinkText(rate) == QStringLiteral("PCIe Gen %1").arg(generation), "generation mapping incorrect");
    }
    check(pcieLinkText(8, 16) == QStringLiteral("PCIe Gen 3 ×16 (16 lanes)"), "observed link formatting incorrect");
    check(pcieLinkText(16) == QStringLiteral("PCIe Gen 4"), "unreported lane count invented");
    check(pcieLinkText(10, 4).contains(QStringLiteral("generation unknown")), "unknown rate assigned a generation");
    check(!pcieLinkText(8.01).contains(QStringLiteral("Gen 3")), "unfamiliar rate rounded to known generation");
    check(pcieLinkText(std::numeric_limits<double>::quiet_NaN()) == QStringLiteral("Unavailable"), "NaN accepted");
    check(pcieLinkText(0) == QStringLiteral("Unavailable"), "zero rate accepted");
    return failures ? 1 : 0;
}
