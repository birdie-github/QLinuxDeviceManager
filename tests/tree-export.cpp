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
    // A tiny PNG fixture needs no graphical application or live icon theme.
    const QByteArray png = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a9WQAAAAASUVORK5CYII=");
    tree.icons = {png};
    tree.rows[0].icon = 0;
    tree.rows[1].icon = 0;
    tree.rows[2].icon = 0;
    tree.rows[3].icon = 999; // Invalid/missing icons must not remove or break labels.
    tree.rows[4].icon = -2;
    const QByteArray decorated = treeExportHtml(tree);
    check(decorated.count("data:image/png;base64,") == 1
          && decorated.count("<use href=\"#tree-icon-0\"") == 3
          && decorated.contains(png.toBase64()),
          "Repeated row icons reuse one embedded PNG definition without external files");
    check(!decorated.contains("#tree-icon-999") && !decorated.contains("#tree-icon--2")
          && decorated.contains("&lt;script&gt;alert")
          && decorated.contains(QString::fromUtf8("Камера ♡").toUtf8()),
          "Missing icons preserve escaped labels and Unicode text");
    check(treeExportText(tree) == plain, "Embedded icons do not change plain-text export");
    check(!decorated.contains("<footer>"), "Other themes do not receive Icons8 attribution");
    tree.icons8 = true;
    const QByteArray credited = treeExportHtml(tree);
    check(credited.contains("<footer><p>") && credited.contains("Copyright Icons8.")
          && credited.contains("href=\"https://icons8.com/\"")
          && credited.contains("excluded from GPLv3")
          && credited.contains("independent extraction, redistribution or reuse requires a license from Icons8.")
          && credited.indexOf("<footer>") > credited.lastIndexOf("</li>")
          && credited.indexOf("</footer>") < credited.indexOf("</body>"),
          "Icons8 exports carry the full application notice at the bottom of the report");
    check(treeExportText(tree) == plain, "Copyright attribution is confined to HTML export");
    return failures ? 1 : 0;
}
