#pragma once
#include <final/final.h>
#include <QString>

class SearchDialog final : public finalcut::FDialog {
public:
    SearchDialog(const QString &query, finalcut::FWidget *parent);
    QString query() const;
private:
    void adjustSize() override;
    void initLayout() override;
    finalcut::FLineEdit input_{this};
    finalcut::FButton find_{"&Find", this};
    finalcut::FButton cancel_{"&Cancel", this};
};
