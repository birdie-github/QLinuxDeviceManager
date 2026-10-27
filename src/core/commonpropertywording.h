#pragma once
#include <QCoreApplication>

// Presentation only: hardware collection, field IDs and read states stay shared.
enum class PropertyPresentation { Gui, Tui };

namespace CommonPropertyWording {
struct Text {
    const char *gui;
    const char *tui;
};

// Keep corresponding GUI and TUI wording together. Use nullptr to omit a field.
inline constexpr Text unverifiedPresence {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Present in the last inventory; properties have not been verified."),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Inventory snapshot; properties unverified.")};
inline constexpr Text verifiedPresence {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Present at the last property read."),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Present at last property read.")};
inline constexpr Text unboundStatus {
    QT_TRANSLATE_NOOP("PropertiesDialog", "No directly bound driver; this alone is not a fault."),
    QT_TRANSLATE_NOOP("PropertiesDialog", "No driver bound; not necessarily a fault.")};
inline constexpr Text unboundDriver {
    QT_TRANSLATE_NOOP("PropertiesDialog", "No directly bound driver"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "No driver bound")};
inline constexpr Text driver {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Bound kernel driver"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Kernel driver")};
inline constexpr Text module {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Owning kernel module"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Kernel module")};
inline constexpr Text moduleType {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Built-in or modular"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Module type")};
inline constexpr Text modular {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Modular (kernel initstate evidence)"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Modular (kernel initstate)")};
inline constexpr Text undetermined {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Undetermined; a missing module link/version does not prove a built-in driver."),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Undetermined")};
inline constexpr Text runtimeVersion {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Running module version"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Running version")};
inline constexpr Text moduleFile {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Installed module filename"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Module file")};
inline constexpr Text installedVersion {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Installed module version"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Installed version")};
inline constexpr Text description {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Installed module description"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Description")};
inline constexpr Text author {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Installed module author"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Author")};
inline constexpr Text license {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Installed module license"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "License")};
inline constexpr Text firmwareNames {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Declared firmware names"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Firmware names")};
inline constexpr Text firmwareRevision {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Reported device firmware revision"),
    QT_TRANSLATE_NOOP("PropertiesDialog", "Device firmware revision")};
inline constexpr Text metadataScope {
    QT_TRANSLATE_NOOP("PropertiesDialog", "Installed metadata may differ from code already loaded before an update. Declared firmware names do not prove firmware is loaded or report its version."),
    nullptr};

inline const char *source(Text wording, PropertyPresentation presentation)
{
    return presentation == PropertyPresentation::Tui ? wording.tui : wording.gui;
}
inline QString translated(Text wording, PropertyPresentation presentation)
{
    const char *value = source(wording, presentation);
    return value ? QCoreApplication::translate("PropertiesDialog", value) : QString();
}
} // namespace CommonPropertyWording
