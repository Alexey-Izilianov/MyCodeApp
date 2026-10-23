#include "vcs.h"

#include <QDir>
#include <QFileInfo>

#include "gitrepo.h"
#include "hgrepo.h"

namespace vcs {

std::unique_ptr<Repository> discover(const QString &path)
{
    // Ближайший вверх по папкам: вложенный репозиторий важнее внешнего
    for (QDir dir(path); ; ) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral(".git"))))
            return git::Repository::discover(dir.path());
        if (QFileInfo(dir.filePath(QStringLiteral(".hg"))).isDir())
            return hg::Repository::open(dir.path());
        if (!dir.cdUp())
            return nullptr;
    }
}

bool init(Kind kind, const QString &path, QString *error)
{
    return kind == Kind::Git ? git::Repository::init(path, error) : hg::Repository::init(path, error);
}

QVector<GraphRow> layoutGraph(const QVector<Commit> &commits)
{
    struct Lane {
        QString id; // коммит, которого ждёт дорожка
        int color = 0;
    };
    QVector<Lane> lanes;
    QVector<GraphRow> rows;
    rows.reserve(commits.size());
    int colors = 0;
    for (const Commit &c : commits) {
        GraphRow row;
        const QVector<Lane> top = lanes;
        int lane = -1;
        for (int i = 0; i < top.size() && lane < 0; ++i)
            if (top.at(i).id == c.id)
                lane = i;
        const bool fromTop = lane >= 0;
        if (!fromTop)
            lane = int(top.size()); // вершина ветки — новая дорожка справа
        row.lane = lane;
        row.color = fromTop ? top.at(lane).color : colors++;

        QVector<Lane> next;
        auto toParent = [&](int index) { row.edges.append({lane, index, next.at(index).color, GraphEdge::Out}); };
        for (int i = 0; i < top.size(); ++i) {
            if (top.at(i).id == c.id) { // все дорожки, ждавшие коммит, сходятся в нём
                row.edges.append({i, lane, top.at(i).color, GraphEdge::In});
                if (i == lane && !c.parents.isEmpty()) {
                    next.append({c.parents.first(), row.color});
                    toParent(int(next.size()) - 1);
                }
                continue;
            }
            next.append(top.at(i));
            row.edges.append({i, int(next.size()) - 1, top.at(i).color, GraphEdge::Through});
        }
        if (!fromTop && !c.parents.isEmpty()) {
            next.append({c.parents.first(), row.color});
            toParent(int(next.size()) - 1);
        }
        for (int p = 1; p < c.parents.size(); ++p) { // слияние: остальные родители
            int at = -1;
            for (int i = 0; i < next.size() && at < 0; ++i)
                if (next.at(i).id == c.parents.at(p))
                    at = i;
            if (at < 0) {
                next.append({c.parents.at(p), colors++});
                at = int(next.size()) - 1;
            }
            toParent(at);
        }
        row.width = qMax(int(top.size()) + (fromTop ? 0 : 1), int(next.size()));
        lanes = std::move(next);
        rows.append(std::move(row));
    }
    return rows;
}

} // namespace vcs
