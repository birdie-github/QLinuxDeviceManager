#include "gpulinksample.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char *why) {
        if (!ok) { std::fprintf(stderr, "%s\n", why); ++failures; }
    };
    GpuLinkSample idle, loaded;
    check(parseGpuLinkSample("2.5 GT/s PCIe", "16", idle), "idle link rejected");
    check(parseGpuLinkSample("16.0 GT/s PCIe", "8", loaded), "loaded link rejected");
    check(loaded.exceeds(idle), "speed ordering incorrect");
    GpuLinkSample peak = loaded;
    if (idle.exceeds(peak)) peak = idle;
    check(peak.speed == 16 && peak.width == 8, "combined maxima fabricated a 16 GT/s x16 sample");
    check(GpuLinkSample{16, 16}.exceeds(peak), "same-speed width ordering incorrect");
    for (const auto &bad : {"Unknown", "0 GT/s PCIe", "-2.5 GT/s PCIe", "nan GT/s PCIe",
                           "inf GT/s PCIe", "2.5 GB/s PCIe", "2.5 GT/s PCIe trailing", "2048 GT/s PCIe"})
        check(!parseGpuLinkSample(bad, "16", loaded), "invalid speed accepted");
    for (const auto &bad : {"", "0", "-1", "999999999999999", "65", "x16", "16junk"})
        check(!parseGpuLinkSample("8.0 GT/s PCIe", bad, loaded), "invalid width accepted");
    // Integrated/root-complex functions may expose these kernel sentinel values.
    check(!parseGpuLinkSample("Unknown", "0", loaded), "unknown laptop link accepted");
    check(!parseGpuLinkSample("8.0 GT/s PCIe", "255", loaded), "unknown width sentinel accepted");
    check(loaded.speed == 16 && loaded.width == 8, "failed parse modified the previous observation");
    return failures ? 1 : 0;
}
