#include "displayidentity.h"
#include <QCoreApplication>
#include <cstdio>
#include <cerrno>

namespace {
QByteArray descriptor(unsigned char serial)
{
    QByteArray bytes(128, '\0');
    bytes.replace(0, 8, QByteArray::fromHex("00ffffffffffff00"));
    bytes[12] = static_cast<char>(serial);
    bytes[18] = 1;
    bytes[19] = 3;
    unsigned sum = 0;
    for (int i = 0; i < 127; ++i) sum += static_cast<unsigned char>(bytes[i]);
    bytes[127] = static_cast<char>((256 - (sum & 255)) & 255);
    return bytes;
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    const QByteArray selected = descriptor(1), other = descriptor(2);
    check(DisplayIdentity::validEdid(selected), "A complete EDID 1.3 descriptor is usable for identity");
    // Connector names/numbering are deliberately absent from this fallback:
    // HDMI-A-1 can match HDMI-0 without assuming either numbering convention.
    auto match = DisplayIdentity::matchEdid(selected, {selected, other}, {other, selected});
    check(match.status.state == ReadState::Available && match.output == 1,
          "Fallback follows descriptor identity rather than output order or numbering");
    match = DisplayIdentity::matchEdid(selected, {selected, selected}, {selected});
    check(match.status.state == ReadState::Error && match.status.error == ENOTUNIQ && match.output == -1,
          "Duplicate kernel EDIDs cannot identify a physical connector");
    match = DisplayIdentity::matchEdid(selected, {selected}, {selected, selected});
    check(match.status.state == ReadState::Error && match.status.error == ENOTUNIQ && match.output == -1,
          "Duplicate session EDIDs cannot identify an X11 output");
    match = DisplayIdentity::matchEdid(selected, {selected}, {other});
    check(match.status.state == ReadState::Unavailable && match.output == -1,
          "A different monitor cannot supply the selected display's session settings");
    check(DisplayIdentity::matchEdid(selected, {other}, {selected}).status.state == ReadState::Unavailable,
          "A selected descriptor absent from the kernel snapshot cannot be matched");
    check(DisplayIdentity::matchEdid(selected, {selected, {}}, {selected}).status.state == ReadState::Unavailable
          && DisplayIdentity::matchEdid(selected, {selected}, {selected, {}}).status.state == ReadState::Unavailable,
          "Missing identities on either side prevent a supposedly unique fallback");
    QByteArray invalid = selected;
    invalid[20] = 1;
    check(!DisplayIdentity::validEdid(invalid)
          && DisplayIdentity::matchEdid(invalid, {invalid}, {invalid}).status.state == ReadState::Unavailable,
          "Identical corrupt EDIDs do not prove a match");
    check(!DisplayIdentity::validEdid(selected.left(127)) && !DisplayIdentity::validEdid({}),
          "Truncated and empty descriptors are rejected");
    QByteArray extended = selected;
    extended[126] = 1;
    extended[127] = static_cast<char>(static_cast<unsigned char>(extended[127]) - 1);
    check(!DisplayIdentity::validEdid(extended), "A declared extension must be present");
    QByteArray extension(128, '\0');
    extension[0] = 2;
    extension[127] = static_cast<char>(254);
    extended += extension;
    check(DisplayIdentity::validEdid(extended), "Every complete extension participates in EDID validation");
    check(DisplayIdentity::matchEdid(selected, {selected}, {extended}).status.state == ReadState::Unavailable,
          "Matching uses the full descriptor, not a shared manufacturer/model or base block");
    extended[130] = 1;
    check(!DisplayIdentity::validEdid(extended), "A bad extension checksum invalidates the descriptor");
    check(!DisplayIdentity::validEdid(QByteArray(DisplayIdentity::edidLimit + 1, '\0')),
          "Oversized descriptors cannot enter matching");
    return failures ? 1 : 0;
}
