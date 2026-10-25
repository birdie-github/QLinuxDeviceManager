#include "searchdialog.h"
#include <algorithm>

using namespace finalcut;

SearchDialog::SearchDialog(const QString &query, FWidget *parent) : FDialog(parent)
{
    setText("Find device (current view)");
    input_.setLabelText("&Text");
    input_.setText(FString(query.toStdWString()));
    input_.addCallback("activate", [this] { done(ResultCode::Accept); });
    find_.addCallback("clicked", [this] { done(ResultCode::Accept); });
    cancel_.addCallback("clicked", [this] { done(ResultCode::Reject); });
}

QString SearchDialog::query() const
{
    return QString::fromStdWString(input_.getText().toWString()).trimmed();
}

void SearchDialog::adjustSize()
{
    const auto width = std::min<std::size_t>(64, std::max<std::size_t>(30, getDesktopWidth()));
    const int x = getDesktopWidth() > width ? int((getDesktopWidth() - width) / 2) + 1 : 1;
    setGeometry(FPoint{x, 3}, FSize{width, 8}, false);
    input_.setGeometry(FPoint{8, 2}, FSize{width - 11, 1});
    find_.setGeometry(FPoint{int(width) - 26, 5}, FSize{10, 1});
    cancel_.setGeometry(FPoint{int(width) - 13, 5}, FSize{10, 1});
    FDialog::adjustSize();
}

void SearchDialog::initLayout()
{
    adjustSize();
    FDialog::initLayout();
    input_.setFocus();
}
