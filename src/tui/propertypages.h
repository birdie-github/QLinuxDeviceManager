#pragma once
#include "commonproperties.h"

// Adapt the inventory without claiming that on-demand properties were read.
CommonProperties inventoryCommonProperties(const Device &device);
struct PropertyPage {
    QString text;
    QVector<int> shadedLines; // Physical lines of alternating logical fields.
};
PropertyPage commonPropertyPage(const QVector<PropertyEntry> &entries, int tab, std::size_t columns = 0);
PropertyPage inventoryDetailsPage(const Device &device);
