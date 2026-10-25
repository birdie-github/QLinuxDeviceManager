#include "treeexport.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    TreeExportSnapshot tree {"Devices <by> connection", "<script>search</script>", "2026-10-25T12:00:00Z", {
        {"Host & computer", 0, true, true},
        {"Controller", 1, true, false},
        {"Disk\nwith\ttabs", 2, false, false},
        {"<script>alert('x')</script>", 1, false, false},
        {QString::fromUtf8("Камера ♡"), 1, false, false}}};
    const QByteArray plain = treeExportText(tree);
    check(plain.startsWith("Host & computer\n  Controller\n    Disk with tabs\n"),
        "Text uses two spaces per level and includes collapsed descendants");
    check(plain.contains(QString::fromUtf8("Камера ♡").toUtf8()), "Text preserves UTF-8 labels");
    const QByteArray html = treeExportHtml(tree);
    check(html.contains("<details open><summary>Host &amp; computer</summary>")
          && html.contains("<details><summary>Controller</summary>"),
          "HTML preserves branch expansion states");
    check(!html.contains("<script>alert") && !html.contains("<script>search")
          && html.contains("&lt;script&gt;") && html.contains("Devices &lt;by&gt; connection"),
          "HTML escapes labels, view names and search text");
    check(html.count("<details") == html.count("</details>")
          && html.count("<ul") == html.count("</ul>")
          && html.count("<li>") == html.count("</li>"), "HTML closes nested branches and leaves");
    check(html.contains("id=\"expand\"") && html.contains("id=\"collapse\"")
          && !html.contains("<script src=") && !html.contains("<link "),
          "HTML supplies offline expansion controls without external assets");
    check(treeExportText({}).isEmpty() && treeExportHtml({}).contains("</html>"),
          "Empty trees serialize safely");
    return failures ? 1 : 0;
}
